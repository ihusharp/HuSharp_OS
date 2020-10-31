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

// 用来存储 inode 位于 扇区的位置
struct inode_position {
    bool two_sec;   // 判断 inode 是否跨扇区，适用于那些位于第一个扇区末尾，但是扇区末尾大小不够 inode 大小
    uint32_t sec_lba;   // inode 所在扇区号
    uint32_t off_size;  // indoe 所在扇区的字节偏移地址
};

// 获取inode所在的扇区和扇区内的偏移量，将其写入到 inode_pos 中
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
    enum intr_status old_staus = intr_enable();
    
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
    intr_set_status(old_staus);
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
        new_inode->i_sectors[sec_idx++] = 0;
    }
}

