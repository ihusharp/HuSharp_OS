#include "ide.h"
#include "inode.h"
#include "fs.h"
#include "file.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "interrupt.h"
#include "list.h"
#include "stdio-kernel.h"
#include "string.h"
#include "super_block.h"


// extern struct partition;
// 用来存储 inode 位于 扇区的位置
struct inode_position {
    bool two_sec;   // 判断 inode 是否跨扇区，适用于那些位于第一个扇区末尾，但是扇区末尾大小不够 inode 大小
    uint32_t sec_lba;   // inode 所在扇区号
    uint32_t off_size;  // inode 所在扇区的字节偏移地址
};

// 获取 inode 所在的扇区和扇区内的偏移量，将其写入到 inode_pos 中
static void inode_locate(struct partition* part, uint32_t inode_no, struct inode_position* inode_pos) {
    // inode_table 在硬盘上是连续的
    ASSERT(inode_no < 4096);
    // 已经挂载到硬盘上了，因此可以直接访问
    uint32_t inode_table_lba = part->sb->inode_table_lba;

    uint32_t inode_size = sizeof(struct inode);
    // 第 inode_no 个节点 相对于inode_table_lba的字节偏移量
    uint32_t off_size = inode_no * inode_size;  
    uint32_t off_sec = off_size / 512;  // 扇区偏移量
    // 待查找的inode所在扇区中的起始地址
    uint32_t off_size_in_sec = off_size % 512; 

    // 判断此 i 节点 是否跨越两个扇区
    uint32_t left_in_sec = 512 - off_size_in_sec;// 起始地址距离末尾的字节大小
    if(left_in_sec < inode_size) {// 所扇区剩余字节不足一个 inode
        inode_pos->two_sec = true;
    } else {
        inode_pos->two_sec = false;
    }
    inode_pos->sec_lba = inode_table_lba + off_sec;// 绝对扇区地址
    inode_pos->off_size = off_size_in_sec;// offset 
}

// 将 inode 写入到 分区 part中 
void inode_sync(struct partition* part, struct inode* inode, void* io_buf) {
    uint8_t inode_no = inode->i_no;
    // 要想往磁盘写入 inode ，必须要先知道 inode 在磁盘中的位置
    struct inode_position inode_pos;
    inode_locate(part, inode_no, &inode_pos);// 写入到 inode_pos 中

    ASSERT(inode_pos.sec_lba <= (part->start_lba + part->sec_cnt));

    /* 硬盘中的inode中的成员inode_tag和i_open_cnts是不需要的,
     * 它们只在内存中记录链表位置和被多少进程共享 */
    struct inode pure_inode;// 因此采用一个局部变量来进行操作
    memcpy(&pure_inode, inode, sizeof(struct inode));// 用一个局部变量进行操作

    // 以下inode的三个成员只存在于内存中, 现在将inode同步到硬盘,清掉这三项即可
    pure_inode.i_open_cnts = 0;
    pure_inode.write_deny = false;// 置为 false ，以保证在硬盘中读出时为可写
    pure_inode.inode_tag.prev = pure_inode.inode_tag.next = NULL;

    // 此缓冲区用于拼接同步的inode数据
    char* inode_buf = (char*)io_buf;
    if (inode_pos.two_sec) {//为两个扇区数据
        // 读写硬盘是以扇区为单位,若写入的数据小于一扇区,
        // 要将原硬盘上的内容先读出来再和新数据拼成一扇区后再写入
        // 因此先读出硬盘里的两个扇区数据
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
        // 将待写入的 inode 拼入到两个扇区中的相应位置
        memcpy((inode_buf + inode_pos.off_size), &pure_inode, sizeof(struct inode));
        // 再将拼接好的数据写到磁盘中
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
    } else { // 一个扇区数据，与两个扇区大致一样
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
        // 将待写入的 inode 拼入到两个扇区中的相应位置
        memcpy((inode_buf + inode_pos.off_size), &pure_inode, sizeof(struct inode));
        // 再将拼接好的数据写到磁盘中
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
    }

}


// 根据 inode 节点号返回相应的 inode 节点指针
struct inode* inode_open(struct partition* part, uint32_t inode_no) {   
    // 之前已经为每一个 分区 创建好了一个 inode 队列，作为 inode 的缓存
    // 用来减少对 硬盘的读写操作
    // 因此每查找一个 inode时，先在 此缓存中查找是否之前缓存过
    struct list_elem* elem = part->open_inodes.head.next;// 头指针
    struct inode* inode_found;
    while(elem != &part->open_inodes.tail) {//遍历查找
        inode_found = elem2entry(struct inode, inode_tag, elem);// 找到 inode 入口
        if(inode_found->i_no == inode_no) {//找到了
            inode_found->i_open_cnts++;
            return inode_found;// 找到便直接返回
        }
        elem = elem->next;
    }// 至此 表示没有找到，需要到 硬盘中进行读取

    struct inode_position inode_pos;
    inode_locate(part, inode_no, &inode_pos);// 写入到 inode_pos 中

    ASSERT(inode_pos.sec_lba <= (part->start_lba + part->sec_cnt));

    /* 为使通过sys_malloc创建的新inode被所有任务共享,
     * 需要将inode置于内核空间,故需要临时
     * 将cur_pbc->pgdir置为NULL 
     * 以暂时将此处设为 内核 */
    struct task_struct* cur = running_thread();
    // 由于此处没有区分是内核线程还是用户进行，直接进行的赋值，因此需要先保存原有值
    uint32_t* cur_pagedir_bak = cur->pgdir;
    cur->pgdir = NULL;
    // 至此 分配的内存将位于 内核区
    inode_found = (struct inode*)sys_malloc(sizeof(struct inode));
    // 恢复 pgdir 
    cur->pgdir = cur_pagedir_bak;

    // 现在开始读取硬盘
    char* inode_buf;
    if(inode_pos.two_sec) {// 考虑跨扇区的情况
        inode_buf = (char*)sys_malloc(1024);
        // inode 节点表是被 partition_format 函数连续写入扇区
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);

    } else{// 分配一个扇区大小的缓冲区足够
        inode_buf = (char*)sys_malloc(512);
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
    }
    // 将 inode 复制到 inode_buf 中
    memcpy(inode_found, inode_buf + inode_pos.off_size, sizeof(struct inode));    

    // 根据局部性原理，现在用到的 inode， 等下大概率还会用到
    // 因此放到缓存最前面
    list_push(&part->open_inodes, &inode_found->inode_tag);
    inode_found->i_open_cnts = 1;// 将打开数置为 1

    sys_free(inode_buf);// 释放缓冲区
    return inode_found;
}

// 关闭inode或减少inode的打开数
void inode_close(struct inode* inode) {
    enum intr_status old_status = intr_enable();
    
    if(--inode->i_open_cnts == 0) {//说明此 inode 未被打开, 只是在之前将 inode 加载到内核缓存中
        list_remove(&inode->inode_tag);// 将 inode 从缓存中去除
    /* inode_open时为实现inode被所有进程共享,
     * 已经在sys_malloc为inode分配了内核空间,
     * 释放inode时也要确保释放的是内核内存池 
     * 因为此时可能是 用户进程释放
     * 因此同样需要 打开 pgdir*/
        struct task_struct* cur = running_thread();
        uint32_t* cur_pagedir_bak = cur->pgdir;
        cur->pgdir = NULL;
        sys_free(inode);
        cur->pgdir = cur_pagedir_bak;        
    }
    intr_set_status(old_status);
}

// 将硬盘分区 part 上的 inode 清空
// 将 inode_table 上擦除
void inode_delete(struct partition* part, uint32_t inode_no, void* io_buf) {
    ASSERT(inode_no < 4096);
    struct inode_position inode_pos;
    inode_locate(part, inode_no, &inode_pos);// 存入 inode_pos
    ASSERT(inode_pos.sec_lba <= (part->start_lba + part->sec_cnt));

    char* inode_buf = (char*)io_buf;// 保存变量
    if(inode_pos.two_sec) {//判断是否跨扇区, 以此判断读几个扇区
        // 1、 要读两个扇区
        // 先将原硬盘数据读出
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);// inode_buf 中表示第几个扇区
        // 将 inode_buf 清空
        memset((inode_pos.off_size + inode_buf), 0, sizeof(struct inode));
        // 清 0 的内存数据覆盖磁盘
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
    } else {
                // 2、 只用读一个扇区
        // 先将原硬盘数据读出
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);// inode_buf 中表示第几个扇区
        // 将 inode_buf 清空
        memset((inode_pos.off_size + inode_buf), 0, sizeof(struct inode));
        // 清 0 的内存数据覆盖磁盘
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
    }


}

// 回收 inode 数据块 和 inode 本身在 bitmap 中的位置
void inode_release(struct partition* part, uint32_t inode_no) {
    struct inode* inode_to_del = inode_open(part, inode_no);
    ASSERT(inode_to_del->i_no == inode_no);

    // 回收 inode 占用的所有块
    uint8_t block_idx = 0, block_cnt = 12;// block_cnt 表示回收块数
    uint32_t all_blocks[140] = {0}; // all_blocks 就不用说明了
    uint32_t block_bitmap_idx;// 回收块位图

    // 1、 先存入 12 个直接块
    while(block_idx < 12) {
        all_blocks[block_idx] = inode_to_del->i_sectors[block_idx];
        block_idx++;
    }

    // 2、 如果一级间接块存在， 那么将 128 个间接块读入到 all_blocks[12~]中
    if(inode_to_del->i_sectors[12] != 0) {
        ide_read(part->my_disk, inode_to_del->i_sectors[12], all_blocks + 12, 1);
        block_cnt = 140;

        // 回收一级间接块
        // 由于 一级间接块占用一个块， 因此要从 块位图 中删除
        block_bitmap_idx = inode_to_del->i_sectors[12] - part->sb->data_start_lba;
        ASSERT(block_bitmap_idx > 0);
        bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
        bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
    }

    // 3、 至此， inode 所有块地址都存入到了 all_blocks 中
    // 现在开始逐个回收
    block_idx = 0;
    while(block_idx < block_cnt) {// 若存在 间接块，那么为 140
        if(all_blocks[block_idx] != 0) {
            block_bitmap_idx = 0;
            block_bitmap_idx = all_blocks[block_idx] - part->sb->data_start_lba;
            ASSERT(block_bitmap_idx > 0);
            bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
            bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
        }
        block_idx++;// 下一个块
    }
    /******     此处有个值得注意的点    ******/
    /* 若删除为文件， 那么并不需要遍历所有块， 只用遍历到 数据为空处便可以
     * 因为文件中的数据是顺序存取的
     * 而如果是目录的话，那么就需要遍历所有， 因为其中的文件可能被删除
     * 为简便结构统一， 便干脆对 140 块都进行遍历
     */
    // 至此 已经释放完 数据 sectors 所占用的块了
    // 4、 开始释放 inode 占用空间
    bitmap_set(&part->inode_bitmap, inode_no, 0);
    bitmap_sync(cur_part, inode_no, INODE_BITMAP);

    /******     以下inode_delete是调试用的    ******
   * 此函数会在inode_table中将此inode清0,
   * 但实际上是不需要的,inode分配是由inode位图控制的,
   * 硬盘上的数据不需要清0,可以直接覆盖*/
  // 此处之所以这么做， 是为了之后展示文件被删除更直观， 实际没必要， 位图直接置 0 即可。
    void* io_buf = sys_malloc(1024);// 为 1024 即两个扇区， 是由于 inode 可能跨扇区
    inode_delete(part, inode_no, io_buf);
    sys_free(io_buf);//释放
    /***********************************/
    inode_close(inode_to_del);
}


// 初始化 new_inode
void inode_init(uint32_t inode_no, struct inode* new_inode) {
    new_inode->i_no = inode_no;
    new_inode->i_size = 0;
    new_inode->i_open_cnts = 0;
    new_inode->write_deny = false;// 此时是否打开 写文件不能并行进行

    // 初始化索引数组
    uint8_t sec_idx = 0;
    while (sec_idx < 13) {
        new_inode->i_sectors[sec_idx] = 0;
        sec_idx++;
    }
}

