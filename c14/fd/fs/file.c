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
    uint32_t fd_idx = 3;// 跨过 stdin、 stdout、 stderr
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

// globa_fd_idx 表示 全局描述符 的 下标, 也就是 数组 file_table 的下标
// 将全局描述符下标安装到进程或线程自己的文件描述符数组fd_table中,
// 成功返回下标, 失败返回-1 
int32_t pcb_fd_install(int32_t globa_fd_idx) {
    struct task_struct* cur = running_thread();
    uint8_t local_fd_idx = 3;// 跨过 stdin、 stdout、 stderr
    while (local_fd_idx < MAX_FILES_OPEN_PER_PROC) {
        if(cur->fd_table[local_fd_idx] == -1) {// -1 表示 free_slot ，可用
            cur->fd_table[local_fd_idx] = globa_fd_idx;
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
    ide_write(part->my_disk, sec_lba, bitmap_off, 1);
}// 写入一个扇区


// 创建文件， 若成功则返回文件描述符 ， 否则返回 -1
// 在 parent_dir 中， 以 flag 格式创建 filename
int32_t file_create(struct dir* parent_dir, char* filename, uint8_t flag) {
    // 1、 分配 inode， inode_bitmap 、inode_cnt
    // 2、 

    // 后续操作的公共缓冲区
    // 由于 将其写入硬盘是必须的 也是 最后一步
    // 因此最好一开始就分配好内存， 以免最后内存不够导致之前步骤白费
    // 一般读写是一个扇区， 为防止跨扇区， 因此申请 2 个扇区
    void* io_buf = sys_malloc(1024);
    if(io_buf == NULL) {
        printk("in file_create: sys_malloc for io_buf failed!\n");
        return -1;
    }

    uint8_t rollback_step = 0;// 记录回滚各资源状态

    // 创建文件 inode -> 文件描述符 fd -> 目录项 -> 写入硬盘

    // 首先创办 inode 
    int32_t inode_no = inode_bitmap_alloc(cur_part);
    if(inode_no == -1) {
        printk("in file_create: inode_allocing failed!\n");
        return -1;
    }

    // 此 inode 应该在堆中申请， 不得由局部变量得到（局部变量会在函数退出时释放
    struct inode* new_file_inode = (struct inode*)sys_malloc(sizeof(struct inode*));
    if(new_file_inode == NULL) {
        printk("in file_create: sys_malloc for inode_allocing failed!\n");
        // 此时 由于改变了 inode_bitmap 因此需要回滚
        rollback_step = 1;
        goto rollback;
    }// 至此 表示创建成功
    inode_init(inode_no, new_file_inode);// init

    // 写入到 fd
    int32_t fd_idx = get_free_slot_in_global();
    if(fd_idx == -1) {
        printk("in file_create: exceed max open files!\n");
        rollback_step = 2;
        goto rollback;
    }   

    // 初始化 文件表中的文件结构
    file_table[fd_idx].fd_inode = new_file_inode;
    file_table[fd_idx].fd_pos = 0;
    file_table[fd_idx].fd_flag = flag;
    file_table[fd_idx].fd_inode->write_deny = false;

    // 开始创建目录项
    struct dir_entry new_dir_entry;
    memset(&new_dir_entry, 0, sizeof(struct dir_entry));

     // create_dir_entry只是内存操作不出意外,不会返回失败
    create_dir_entry(filename, inode_no, HS_FT_REGULAR, &new_dir_entry);

    // 写入到 父目录中
    if(!sync_dir_entry(parent_dir, &new_dir_entry, io_buf)) {
        printk("in file_create: sync_dir_entry failed!\n");
        rollback_step = 3;
        goto rollback;
    }

    // 开始同步到硬盘中， 持久化
    // 以下同步一般不会出现问题， 因此没有安排回滚
    // sync_dir_entry 会改变父目录 inode 信息， 因此 父目录的inode 也需要同步
    memset(io_buf, 0, 1024);
    inode_sync(cur_part, parent_dir->inode, io_buf);

    // 新文件 inode 同步
    memset(io_buf, 0, 1024);
    inode_sync(cur_part, new_file_inode, io_buf);

    // bitmap 同步
    bitmap_sync(cur_part, inode_no, INODE_BITMAP);

    // 将创建的 inode 添加到 open_inodes 链表中
    list_push(&cur_part->open_inodes, &new_file_inode->inode_tag);
    new_file_inode->i_open_cnts = 1;// 将该文件被打开数 置为 1

    sys_free(io_buf);// 至此 已经同步完所有步骤
    return pcb_fd_install(fd_idx);


rollback:
    switch (rollback_step) // 值得注意的是， 由于此处的 几个情况回滚是循序渐进的， 因此不采用 break
    {
    case 3:// 表示 目录项 写入到 父目录中失败， 应当将 写入进 file_table 中的 fd 回滚
        memset(&file_table[fd_idx], 0, sizeof(struct file));
    case 2:// 表示 fd 分配超过 PCB 的最大文件数
        sys_free(new_file_inode);
    case 1:// inode 节点创建失败， 需要回滚之前 inode_bitmap 中分配的 新inode
        bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
        break;
    default:
        break;
    }

}
