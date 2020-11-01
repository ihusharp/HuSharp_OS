#include "file.h"
#include "fs.h"
#include "super_block.h"
#include "inode.h"
#include "stdio-kernel.h"
#include "memory.h"
#include "debug.h"
#include "interrupt.h"
#include "string.h"
#include "thread.h"
#include "global.h"

#define MAX_FILES_OPEN_PER_PROC 8

// 文件表
// 表示的是将文件打开的次数，而非文件个数
// 因为文件可以多次打开，也会占表
struct file file_table[MAX_FILE_OPEN];

// 从文件表file_table中获取一个空闲位,成功返回下标,失败返回-1
int32_t get_free_slot_in_global(void) {
    uint32_t fd_idx = 3;
    while(fd_idx < MAX_FILE_OPEN) {
        if(file_table[fd_idx].fd_inode == NULL) {
            // break;// 说明找到了
            return fd_idx;
        }
        fd_idx++;
    }// 至此 说明还未找到
    if(fd_idx == MAX_FILE_OPEN) {
        printk("exceed max open files!\n");
        return -1;
    } else {
        printk("the error fd_idx is %d", &fd_idx);
    }
    // return fd_idx;
}

// globa_fd_idx 表示 全局描述符 的 下标
// 将全局描述符下标安装到进程或线程自己的文件描述符数组fd_table中,
// 成功返回下标, 失败返回-1 
int32_t pcb_fd_install(int32_t globa_fd_idx) {
    struct task_struct* cur = running_thread();
    uint8_t local_fd_idx = 3;// 跨过 stdin、 stdout、 stderr
    while (local_fd_idx < MAX_FILE_OPEN) {
        if(cur.fd_table[local_fd_idx] == -1) {// -1 表示 free_slot ，可用
            cur.fd_table[local_fd_idx] = globa_fd_idx;
            break;
        }
        local_fd_idx++;
    }
    if(local_fd_idx == MAX_FILES_OPEN_PER_PROC) {
        printk("exceed max open files_per_proc\n");
        return -1;
    }
    return local_fd_idx;
}

//  分配一个i结点,返回i结点号
int32_t inode_bitmap_alloc(struct partition* part) {
    int32_t bit_idx = bitmap_scan(&part->inode_bitmap, 1);
    if(bit_idx == -1) {
        return -1;
    }
    bitmap_set(&part->inode_bitmap, bit_idx, 1);
    return bit_idx;
}

// 分配一个扇区， 返回其扇区地址
int32_t block_bitmap_alloc(struct partition* part) {
    int32_t bit_idx = bitmap_scan(&part->block_bitmap, 1);
    if(bit_idx == -1) {
        return -1;
    }
    bitmap_set(&part->block_bitmap, bit_idx, 1);
    // 和inode_bitmap_malloc不同,此处返回的不是位图索引,而是具体可用的扇区地址
    return (part->sb->data_start_lba + bit_idx);
}

// 将内存中 bitmap 第 bit_idx 位所在的 512 字节同步到硬盘
// 可以为 inode 位图， 也可以为 block 位图
void bitmap_sync(struct partition* part, uint32_t bit_idx, uint8_t btmp_type) {
    uint32_t off_sec = bit_idx / 4096;// 位图中 bit_idx 位置， 以 512bytes = 4096bits 为单位，得到 第几个扇区
    uint32_t off_size = off_sec * BLOCK_SIZE;   // 得到 字节地址
    uint32_t sec_lba;
    uint8_t* bitmap_off;

    // 需要被同步到硬盘的位图只有 inode_bitmap 和 block_bitmap
    switch (btmp_type)
    {
        case INODE_BITMAP:
            sec_lba = part->sb->inode_bitmap_lba + off_sec;// 位图扇区地址
            bitmap_off = part->inode_bitmap.bits + off_size;// bit 为单位
            break;
        case BLOCK_BITMAP:
            sec_lba = part->sb->block_bitmap_lba + off_sec;
            bitmap_off = part->block_bitmap.bits + off_size;
            break;
        default:
            break;
    }
    ide_wite(part->my_disk, sec_lba, bitmap_off, 1);
}// 写入一个扇区



int32_t file_create(struct dir* parent_dir, char* filename, uint8_t flag);


