#include "console.h"
#include "debug.h"
#include "global.h"
#include "interrupt.h"
#include "list.h"
#include "memory.h"
#include "process.h"
#include "string.h"
#include "thread.h"
#include "tss.h"

extern void intr_exit(void);

// 用户进程基于线程实现
// 创建用户进程初始上下文信息
// 参数 filename_ 表示用户程序的名称
// 至于 为啥是 filename_，是由于用户程序必然是通过文件系统加载进来的，因此就用文件名代指用户程序名
void start_process(void* filename_)
{
    // 由于还未实现文件系统，因此采用普通函数代指用户程序
    void* function = filename_;
    struct task_struct* cur = running_thread();
    cur->self_kstack += sizeof(struct thread_stack); //为引用 intr_stack 栈，需要先跨越 thread_stack栈
    // 为啥不直接从 pcb 顶端开始访问 intr_stack?而是得地址从低往高访问？
    // 因为 结构体成员是按照成员顺序，由低到高放置
    // 而 栈是由高地址向低处延伸，因此指针 proc_stack 放到低地址，往上访问，才和成员放置一样
    struct intr_stack* proc_stack = (struct intr_stack*)cur->self_kstack;
    proc_stack->edi = proc_stack->esi = proc_stack->ebp = proc_stack->esp_dummy = 0;
    proc_stack->ebx = proc_stack->edx = proc_stack->ecx = proc_stack->eax = 0;
    proc_stack->gs = 0; // os 不允许 用户进程访问显存段，因此置为 0
    proc_stack->ds = proc_stack->es = proc_stack->fs = SELECTOR_U_DATA;
    // 将 cs eip 指向程序入口地址，function 是最终在线程执行的函数
    proc_stack->eip = function;
    proc_stack->cs = SELECTOR_U_CODE;
    proc_stack->eflags = (EFLAGS_IOPL_0 | EFLAGS_IF_1 | EFLAGS_MBS);
    // 给用户进程分配栈
    // 由于分配地址为内存空间页 的下边界，因此需要分配指定虚拟地址为 0xc0000000- 0x1000
    // 将分配的页地址 + PGSIZE 即可（因为栈底在高地址）
    proc_stack->esp = (void*)((uint32_t)(get_a_page(PF_USER, USER_STACK_3_VADDR)) + PG_SIZE);
    proc_stack->ss = SELECTOR_U_DATA;
    // 将 esp 改为刚刚设置好的栈，从而继续通过 jmp intr_exit ，欺骗CPU 假装是从中断返回
    // 从而进行 一系列的 pop ret 操作
    asm volatile("movl %0, %%esp; jmp intr_exit"
                 :
                 : "g"(proc_stack)
                 : "memory");
}

// 激活页表
// 首先需要明白， cr3 存储页表，而 cr3 只有一个，因此在进程执行前
// 需要将 cr3 值改为进程对应的 页表
void page_dir_activate(struct task_struct* p_thread)
{
    /********************************************************
 * 执行此函数时,当前任务可能是线程。
 * 之所以对线程也要重新安装页表, 原因是上一次被调度的可能是进程,
 * 否则不恢复页表的话,线程就会使用进程的页表了。
 * 因为 线程是使用的是内核页表，而用户进程使用的是自己的页表
 ********************************************************/
    // cr3 中加载的为 物理地址
    // 首先判断是内核线程还是用户进程
    ASSERT(p_thread != NULL);
    uint32_t pagedir_phy_addr = 0x100000; // 默认为内核页目录物理地址，即为 1MB处
    if (p_thread->pgdir != NULL) {
        // pagdir 中存储的是页表的虚拟地址 ，需要转换为 无聊地址
        pagedir_phy_addr = addr_v2p((uint32_t)p_thread->pgdir);
    }
    asm volatile("movl %0, %%cr3"
                 :
                 : "r"(pagedir_phy_addr)
                 : "memory");
}

// 激活线程或进程的页表,更新tss中的esp0为进程的特权级0的栈
void process_activate(struct task_struct* p_thread)
{
    ASSERT(p_thread != NULL);
    // 激活页表
    page_dir_activate(p_thread);

    // 由于发生中断时，处理器会进入到 0 特权级进行保存上下文
    // 如果此时为 用户进程，那么需要在 tss 中找到 esp0
    // 如果此时为 线程，即在内核中，那么不需要前往 tss 中找 esp0
    if (p_thread->pgdir) { //即为用户进程
        update_tss_esp(p_thread);
    }
}

// 创建页目录表,将当前页表的表示内核空间的pde复制,
// 成功则返回页目录的虚拟地址,否则返回-1
// 实现内核的共享化
uint32_t* create_page_dir(void)
{
    // 用户进程的页表不能让用户直接访问到,所以在内核空间来申请
    uint32_t* page_dir_vaddr = get_kernel_pages(1);
    if (page_dir_vaddr == NULL) {
        console_put_str("create_page_dir: get_kernel_page failed!");
        return NULL;
    }
    // 将内核空间页目录表复制到用户空间 复制 768-1023 页表项  768 = 0x300
    memcpy((uint32_t*)((uint32_t)page_dir_vaddr + 0x300 * 4), (uint32_t*)(0xfffff000 + 0x300 * 4), 1024);
    // 更新页目录地址
    uint32_t new_page_dir_phy_addr = addr_v2p((uint32_t)page_dir_vaddr);
    // 更新页目录表的最后一项为 用户页目录表地址
    // 因为可能需要用到扩展用户页目录表的操作
    // 物理地址加上属性值
    page_dir_vaddr[1023] = new_page_dir_phy_addr | PG_US_U | PG_RW_W | PG_P_1;

    return page_dir_vaddr;
}

// 创建用户进程虚拟地址位图
void create_user_vaddr_bitmap(struct task_struct* user_prog)
{
    user_prog->userprog_vaddr.vaddr_start = USER_VADDR_START;
    // 利用向上取整函数，为用户空间所需页面分好位图
    uint32_t bitmap_pg_cnt = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PG_SIZE / 8, PG_SIZE);
    // 记录分配位图指针 和 位图长度
    user_prog->userprog_vaddr.vaddr_bitmap.bits = get_kernel_pages(bitmap_pg_cnt);
    user_prog->userprog_vaddr.vaddr_bitmap.btmp_bytes_len = (0xc0000000 - USER_VADDR_START) / PG_SIZE / 8;
    bitmap_init(&user_prog->userprog_vaddr.vaddr_bitmap);
}

// 创建用户进程
void process_execute(void* filename, char* name)
{
    // pcb 的数据结构，由内核维护进程信息
    struct task_struct* thread = get_kernel_pages(1);
    init_thread(thread, name, default_prio);
    create_user_vaddr_bitmap(thread);
    thread_create(thread, start_process, filename);
    thread->pgdir = create_page_dir();

    enum intr_status old_status = intr_disable();
    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    list_append(&thread_ready_list, &thread->general_tag);

    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
    list_append(&thread_all_list, &thread->all_list_tag);

    intr_set_status(old_status);
}
