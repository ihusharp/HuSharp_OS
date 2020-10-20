#include "memory.h"
#include "thread.h"
#include "debug.h"
#include "global.h"
#include "bitmap.h"
#include "print.h"
#include "stdint.h"
#include "string.h"
#include "sync.h"
#include "interrupt.h"

#define PG_SIZE 4096

/***************  位图地址 ********************
 * 因为0xc009f000是内核主线程栈顶，0xc009e000是内核主线程的pcb.
 * 一个页框大小的位图可表示128M内存, 位图位置安排在地址0xc009a000,
 * 这样本系统最大支持4个页框的位图,即512M */
#define MEM_BITMAP_BASE 0xc009a000

// 返回虚拟地址的 高 10 位(即pde)，与中间 10 位(即pte)
#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22)
#define PTE_IDX(addr) ((addr & 0x003ff000) >> 12)

/* 0xc0000000是内核从虚拟地址3G起. 0x100000意指跨过低端1M内存,
 * 使虚拟地址在逻辑上连续 */
#define K_HEAP_START 0xc0100000

// 内存池结构,生成两个实例用于管理内核内存池和用户内存池
struct pool {
    // 本内存池用到的位图结构,用于管理物理内存
    struct bitmap pool_bitmap;
    uint32_t phy_addr_start; // 本内存池所管理物理内存起始地址
    uint32_t pool_size; //本内存池字节容量
    struct lock lock; // 申请内存时保证互斥
};

// arena 的元信息  12个字节
struct arena {
    struct mem_block_desc* desc;// arena 关联的指针
    // large为 ture 时,cnt表示的是页框数。
    // 否则cnt表示空闲mem_block数量 
    uint32_t cnt;
    bool large;	
};

struct pool kernel_pool, user_pool; // 生成全局内核内存池， 用户内存地址
struct virtual_addr kernel_addr; // 给内核分配虚拟地址
struct mem_block_desc k_block_desc[DESC_CNT];   // 内核内存块描述符数组

/* 在pf表示的虚拟内存池中申请pg_cnt个虚拟页,
 * 成功则返回虚拟页的起始地址, 失败则返回NULL 
 */
static void* vaddr_get(enum pool_flags pf, uint32_t pg_cnt)
{
    int vaddr_start = 0; //用于存储分配的虚拟地址
    int bit_idx_start = -1; // 存储位图扫描函数的返回值
    uint32_t cnt = 0;
    if (pf == PF_KERNEL) { // 说明在内核虚拟池中
        bit_idx_start = bitmap_scan(&kernel_addr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1) { //说明没找到
            return NULL;
        }
        // 将已经选出的内存置为 1 ，表示已经使用
        while (cnt < pg_cnt) {
            bitmap_set(&kernel_addr.vaddr_bitmap, bit_idx_start + cnt++, 1);
        }
        // 虚拟地址应当加上 页表所占的内存
        vaddr_start = kernel_addr.vaddr_start + bit_idx_start * PG_SIZE;
    } else { // 用户进程池 分配
        struct task_struct* cur = running_thread();
        bit_idx_start = bitmap_scan(&cur->userprog_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1) {
            return NULL;
        }
        // 将已经选出的内存置为 1 ，表示已经在使用
        while (cnt < pg_cnt) {
            bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
        }
        // 虚拟地址应当加上 页表所占的内存
        vaddr_start = cur->userprog_vaddr.vaddr_start + bit_idx_start * PG_SIZE;
        /* (0xc0000000 - PG_SIZE)做为用户3级栈已经在start_process被分配 */
        ASSERT((uint32_t)vaddr_start < (0xc0000000 - PG_SIZE));
    }
    return (void*)vaddr_start;
}

/********* 以下两个获取虚拟地址 addr 的 pde & pte 地址，为建立映射做准备 ********/
// 得到虚拟地址 vaddr 对应的 pte 指针
// 构造一个 访问该 pte 的32位地址，欺骗处理器
uint32_t* pte_ptr(uint32_t vaddr)
{
    /* 先访问到页目录表自己（位于1023项目录项中） + \
     * 再用页目录项 pde (页目录内页表的索引)做为pte的索引访问到页表 + \
     * 再用 pte 的索引做为页内偏移
     */
    uint32_t* pte = (uint32_t*)(0xffc00000 + ((vaddr & 0xffc00000) >> 10) + PTE_IDX(vaddr) * 4);
    return pte; // 得到物理页的虚拟地址，即通过这个虚拟地址，可以访问到该物理页（保护模式下必须通过 vaddr）
}

// 得到虚拟地址 vaddr 对应的 pde 指针
// 构造一个 访问该 pde 的32位地址，欺骗处理器
uint32_t* pde_ptr(uint32_t vaddr)
{
    uint32_t* pde = (uint32_t*)(0xfffff000 + PDE_IDX(vaddr) * 4);
    return pde;
}

/* 在m_pool指向的物理内存池中分配1个物理页,
 * 成功则返回页框的物理地址,失败则返回NULL
 */
static void* palloc(struct pool* m_pool)
{
    // 扫描或设置位图要保证原子操作
    int bit_idx = bitmap_scan(&m_pool->pool_bitmap, 1); //找一个空闲物理页面
    if (bit_idx == -1) {
        return NULL;
    }
    bitmap_set(&m_pool->pool_bitmap, bit_idx, 1); //表示已经占用
    // 成功则返回页框的物理地址
    uint32_t page_phyaddr = (m_pool->phy_addr_start + (bit_idx * PG_SIZE));
    return (void*)page_phyaddr;
}

/********使用 pde_ptr 和 pte_ptr 来建立虚拟地址和物理地址的映射 ********/
// 本质上便是将 pte 写入到 获取的 pde 项中，将 物理地址写入到 pte 中
static void page_table_add(void* _vaddr, void* _page_phyaddr)
{
    uint32_t vaddr = (uint32_t)_vaddr;
    uint32_t page_phyaddr = (uint32_t)_page_phyaddr;
    // 以下两个函数都是通过 虚拟地址来获取
    uint32_t* pde = pde_ptr(vaddr);
    uint32_t* pte = pte_ptr(vaddr);

    /************************   注意   *************************
 * 执行*pte,会访问到空的pde。所以确保pde创建完成后才能执行*pte,
 * 否则会引发page_fault。因此在*pde为0时,*pte只能出现在下面else语句块中的*pde后面。
 * *********************************************************/
    // 先在页目录内判断目录项的P位，若为1,则表示该表(pte)已存在
    if (*pde & 0x00000001) { // 通过 P 位来判断目录项是否存在
        ASSERT(!(*pte & 0x00000001)); // 表示 pte 不存在

        // 再判断 pte 是否存在
        if (!(*pte & 0x00000001)) {
            *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
        } else { // 并不会执行到此处，因为此处意思是：pde存在，pte也存在
            // 但是之前的 ASSERT 已经判断了 pde 不存在
            PANIC("pte repeat!");
            *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
        }
    } else { // pde 不存在
        // 需要分配 pte 物理页，采用 plloc 函数
        uint32_t new_pde_phyaddr = (uint32_t)palloc(&kernel_pool); //页表中的页框一律从内核中分配

        *pde = (new_pde_phyaddr | PG_US_U | PG_RW_W | PG_P_1); // 写入到 pde 中

        /* 分配到的物理页地址 new_pde_phyaddr 对应的物理内存清0,(使用时清零比回收后清零高效，因为不知道回收后会不会再使用)
     * 避免里面的陈旧数据变成了页表项,从而让页表混乱.
     * 访问到pde对应的物理地址,用pte取高20位便可.
     * 因为pte是基于该pde对应的物理地址内再寻址,
     * 把低12位置0便是该pde对应的物理页的起始
     */
        // 现需要获得新创建的这个物理页的虚拟地址（保护模式得看虚拟地址！）
        memset(((void*)((int)pte & 0xfffff000)), 0, PG_SIZE); //将该新配物理页清0

        ASSERT(!(*pte & 0x00000001)); // 断言 pte 此时是否存在

        // pte项 此时更改为新建物理页的位置
        *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1); //将 物理页地址 写入到 pte项中
    }
}

// 分配pg_cnt个页空间（物理页),成功则返回起始虚拟地址,失败时返回NULL
void* malloc_page(enum pool_flags pf, uint32_t pg_cnt)
{
    ASSERT(pg_cnt > 0 && pg_cnt < 3840);
    /***********   malloc_page的原理是三个动作的合成:   ***********
    1.通过vaddr_get在虚拟内存池中申请虚拟地址
    2.通过palloc在物理内存池中申请物理页
    3.通过page_table_add将以上得到的虚拟地址和物理地址在页表中完成映射
***************************************************************/
    void* vaddr_start = vaddr_get(pf, pg_cnt);
    if (vaddr_start == NULL) {
        return NULL;
    }

    uint32_t vaddr = (uint32_t)vaddr_start, cnt = pg_cnt;
    struct pool* mem_pool = (pf & PF_KERNEL) ? &kernel_pool : &user_pool;

    // 因为虚拟地址是连续的,但物理地址可以是不连续的,所以逐个做映射
    while (cnt-- > 0) {
        void* page_phyaddr = palloc(mem_pool);
        if (page_phyaddr == NULL) {
            return NULL;
        }
        page_table_add((void*)vaddr, page_phyaddr); // 在页表建立映射
        vaddr += PG_SIZE; // 下一个虚拟页
    }
    return vaddr_start;
}

// 从内核物理内存池中申请pg_cnt页内存,成功则返回其虚拟地址,失败则返回NULL
void* get_kernel_pages(uint32_t pg_cnt)
{
    lock_acquire(&kernel_pool.lock);
    void* vaddr = malloc_page(PF_KERNEL, pg_cnt);
    if (vaddr != NULL) { // 虚拟地址是连续的
        memset(vaddr, 0, pg_cnt * PG_SIZE);
    }
    lock_release(&kernel_pool.lock);
    return vaddr;
}

// 从用户物理内存池中申请pg_cnt页内存,成功则返回其虚拟地址,失败则返回NULL
void* get_user_pages(uint32_t pg_cnt)
{
    lock_acquire(&user_pool.lock);
    void* vaddr = malloc_page(PF_USER, pg_cnt);
    //if (vaddr != NULL) { // 虚拟地址是连续的
        memset(vaddr, 0, pg_cnt * PG_SIZE);
    //}
    lock_release(&user_pool.lock);
    return vaddr;
}

// 将地址vaddr与pf池中的物理地址关联,仅支持一页空间分配 
// 与 get_user_pages get_kernel_pages 不同之处在于可以指定特定虚拟地址
void* get_a_page(enum pool_flags pf, uint32_t vaddr)
{
    // 判断是哪一个
    struct pool* mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
    lock_acquire(&mem_pool->lock);

    // 先将虚拟地址对应位图置 1
    struct task_struct* cur = running_thread();
    int32_t bit_idx = -1;

    // 若当前是用户进程，那就修改用户进程自己的虚拟地址位图
    // 进程拥有独立地址空间（页表），线程为NULL
    if (cur->pgdir != NULL && pf == PF_USER) {
        bit_idx = (vaddr - cur->userprog_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx > 0);
        bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx, 1);
    } else if(cur->pgdir == NULL && pf == PF_KERNEL) {
        // 线程没有独立地址空间, 为NULL
        bit_idx = (vaddr - kernel_addr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx > 0);
        bitmap_set(&kernel_addr.vaddr_bitmap, bit_idx, 1);
    } else {
        PANIC("get_a_page:not allow kernel alloc userspace or user alloc kernelspace by get_a_page\n");
    }

    void* page_phyaddr = palloc(mem_pool);
    if (page_phyaddr == NULL) {
        return NULL;
    }
    page_table_add((void*)vaddr, page_phyaddr);
    lock_release(&mem_pool->lock);
    return (void*)vaddr;
}

// 得到虚拟地址映射到的物理地址
uint32_t addr_v2p(uint32_t vaddr) {
    // (*pte)的值是页表所在的物理页框地址,
    uint32_t* pte = pte_ptr(vaddr);
    // 物理页为自然页，低 12 位地址为 0，因此将 虚拟地址的低 12 位 加上便可
    return ((*pte & 0xfffff000) + (vaddr & 0x00000fff));
}

// 初始化内存池, 通过all_mem 初始化物理内存池相关结构
static void mem_pool_init(uint32_t all_mem)
{
    put_str("   phy_mem_pool_init start!\n");
    // 页表大小= 1页的页目录表+第0和第768个页目录项指向同一个页表+
    // 第769~1022个页目录项共指向254个页表,共256个页框
    uint32_t page_table_size = PG_SIZE * 256;
    // 用于记录当前已经使用的内存字节数
    uint32_t used_mem = page_table_size + 0x100000; // 0x100000为低端 1M 内存
    uint32_t free_mem = all_mem - used_mem;
    uint16_t all_free_pages = free_mem / PG_SIZE; //转换为 页数

    // 内核物理内存池 与 用户物理内存池 大小一致
    uint16_t kernel_free_pages = all_free_pages / 2;
    uint16_t user_free_pages = all_free_pages - kernel_free_pages;

    /* 为简化位图操作，余数不处理，坏处是这样做会丢内存。
     * 好处是不用做内存的越界检查,因为位图表示的内存少于实际物理内存
     */
    uint32_t kbm_length = kernel_free_pages / 8; // Kernel BitMap的长度,位图中的一位表示一页,以字节为单位
    uint32_t ubm_length = user_free_pages / 8; // User BitMap的长度.

    uint32_t kp_start = used_mem; // kernel pool start 内核内存池的起始位置
    uint32_t up_start = kp_start + kernel_free_pages * PG_SIZE;

    kernel_pool.phy_addr_start = kp_start;
    user_pool.phy_addr_start = up_start;

    kernel_pool.pool_bitmap.btmp_bytes_len = kbm_length; // 位图字节长度
    user_pool.pool_bitmap.btmp_bytes_len = ubm_length;

    kernel_pool.pool_size = kernel_free_pages * PG_SIZE;
    user_pool.pool_size = user_free_pages * PG_SIZE;

    /*********    内核内存池和用户内存池位图   ***********
 *   位图是全局的数据，长度不固定。
 *   全局或静态的数组需要在编译时知道其长度，
 *   而我们需要根据总内存大小算出需要多少字节。
 *   所以改为指定一块内存来生成位图.
 *   ************************************************/
    // 内核使用的最高地址是0xc009f000,这是主线程的栈地址.(内核的大小预计为70K左右)
    // 32M内存占用的位图是2k.内核内存池的位图先定在MEM_BITMAP_BASE(0xc009a000)处.
    kernel_pool.pool_bitmap.bits = (void*)MEM_BITMAP_BASE;

    // 用户内存池的位图紧跟在内核内存池位图后面
    user_pool.pool_bitmap.bits = (void*)(MEM_BITMAP_BASE + kbm_length);
    /******************** 输出内存池信息 **********************/
    put_str("      kernel_pool_bitmap_start:");
    put_int((int)kernel_pool.pool_bitmap.bits);
    put_str(" kernel_pool_phy_addr_start:");
    put_int(kernel_pool.phy_addr_start);
    put_str("\n");
    put_str("      user_pool_bitmap_start:");
    put_int((int)user_pool.pool_bitmap.bits);
    put_str(" user_pool_phy_addr_start:");
    put_int(user_pool.phy_addr_start);
    put_str("\n");

    /****************    进行初始化    ***************/
    // 将位图 置为 0 表示还未分配
    bitmap_init(&kernel_pool.pool_bitmap);
    bitmap_init(&user_pool.pool_bitmap);

    lock_init(&kernel_pool.lock);
    lock_init(&user_pool.lock);

    // 初始化内核的虚拟地址
    // 用于维护内核堆的虚拟地址,所以要和内核内存池大小一致
    kernel_addr.vaddr_bitmap.btmp_bytes_len = kbm_length;
    // 位图的数组指向一块未使用的内存,目前定位在内核内存池和用户内存池之后
    kernel_addr.vaddr_bitmap.bits = (void*)(MEM_BITMAP_BASE + kbm_length + ubm_length);
    // 虚拟内存池的起始地址，以达到进程在堆中动态申请内存
    kernel_addr.vaddr_start = K_HEAP_START; //内核堆
    bitmap_init(&kernel_addr.vaddr_bitmap);
    put_str("   mem_pool_init done\n");
}

/**********   以下为 堆内存分配函数    ******************/
// 为malloc做准备
void block_desc_init(struct mem_block_desc* desc_array) {	
    uint16_t desc_idx, block_size = 16;

    // 初始化每个 mem_block_desc 的描述符
    for (desc_idx = 0; desc_idx < DESC_CNT; desc_idx++) {
        desc_array[desc_idx].block_size = block_size;

        // 初始化 arena 的内存块数量
        desc_array[desc_idx].blocks_per_arena = (PG_SIZE - sizeof(struct arena)) / block_size;
        list_init(&desc_array[desc_idx].free_list);

        block_size *= 2;
    }// 因此下标越低的 内存块描述符 ，block_size 越小
    
}

//  arena 和 mem_block 的转换函数
// 返回 arena 中第 idx 个内存块的地址
static struct mem_block* arena2block(struct arena* a, uint32_t idx) {
    return (struct mem_block*)((uint32_t)a + sizeof(struct arena) + idx * a->desc->block_size);
}

// 返回内存 b 所在的 arena 地址  ---- arena 占用 1 个页框
static struct arena* block2arena(struct mem_block* b) {
    return (struct arena*)((uint32_t)b & 0xfffff000);
}

// 在堆中分配 size 字节大小内存
void* sys_malloc(uint32_t size) {
    enum pool_flags PF;//指示内核还是用户
    struct pool* mem_pool;
    uint32_t pool_size;
    struct mem_block_desc* descs;
    struct task_struct* cur_thread = running_thread();

    // 判断是哪一个内存池
    if (cur_thread->pgdir == NULL) {//表示为内核线程
        PF = PF_KERNEL;
        mem_pool = &kernel_pool;
        pool_size = kernel_pool.pool_size;
        descs = k_block_desc;
    } else {
        PF = PF_USER;
        mem_pool = &user_pool;
        pool_size = user_pool.pool_size;
        descs = cur_thread->u_block_desc;
    }

    if(!((size > 0) && (size < pool_size))) {// 不满足情况
        return NULL;
    }

    struct arena* a;    // 堆分配内存块单位
    struct mem_block* b;// 内存块
    lock_acquire(&mem_pool->lock);

    // 讨论两种情况：分配内存量是否大于 1024
    if (size > 1024) {// 分配大内存块
        // 只需要一个元信息
        uint32_t page_cnt = DIV_ROUND_UP(size + sizeof(struct arena), PG_SIZE);

        a = malloc_page(PF, page_cnt);// 分配 arena 
        if (a != NULL) {
            memset(a, 0, page_cnt * PG_SIZE); //分配成功则进行清 0

                    // 开始初始化 arena 的元信息
            a->desc = NULL;
            a->large = true;
            a->cnt = page_cnt;
            lock_release(&mem_pool->lock);
            return (void*)(a + 1);// 返回跨过一个 arena 的大小
        } else {
            return NULL;
        }
    } else { // 若申请的内存小于等于1024,可在各种规格的mem_block_desc中去适配
        uint8_t desc_idx;
        // 首先找到合适的 内存块大小
        for (desc_idx = 0; desc_idx < DESC_CNT; desc_idx++) {
            if(size <= descs[desc_idx].block_size) {
                break;
            }
        }// 至此 找到合适的 内存块大小，为 desc_idx 来指示
        // 若 mem
        if(list_empty(&descs[desc_idx].free_list)) {// 若此时没有可用的 arena 
            a = malloc_page(PF, 1);//分配一页框给 新的 arena
            if(a == NULL) {
                lock_release(&mem_pool->lock);
                return NULL;
            } 
            // 现在对 新 arena 进行初始化
            memset(a, 0, PG_SIZE);
            a->large = false;// 为小块
            a->cnt = descs[desc_idx].blocks_per_arena;// 表示现有空闲块
            a->desc = &descs[desc_idx];
        
            uint32_t block_idx;

            enum intr_status old_status = intr_disable();

            // 现在将 arena 拆分成 内存块，将其添加到 各个内存块描述符的 free_list 中
            for (block_idx = 0; block_idx < descs[desc_idx].block_size; block_idx++) {
                b = arena2block(a, block_idx);//返回 arena 中第 idx 个内存块的地址
                ASSERT(!elem_find(&a->desc->free_list, &b->free_elem));
                list_append(&a->desc->free_list, &b->free_elem);// 添加到 free 队列中
            }
            intr_set_status(old_status);
        }   

        // 现在开始分配内存块
        b = elem2entry(struct mem_block, free_elem, list_pop(&(descs[desc_idx]).free_list));
        memset(b, 0, descs[desc_idx].block_size);// 将其初始化
        a = block2arena(b);// 转换获取分配空闲内存块 b  的 arena
        a->cnt--;   //将空闲块个数减 1
        lock_release(&mem_pool->lock);
        return (void*)b;// 返回用户所得到的内存地址。
    }

}


// 内存管理部分初始化入口
void mem_init()
{
    put_str("mem_init start!\n");
    uint32_t mem_bytes_total = (*(uint32_t*)(0xb00)); // 储存机器上物理内存总量
    mem_pool_init(mem_bytes_total); //初始化内存池
    block_desc_init(k_block_desc);
    put_str("mem_init done!\n");
}