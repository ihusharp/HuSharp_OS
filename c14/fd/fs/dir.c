#include "dir.h"
#include "stdint.h"
#include "inode.h"
#include "file.h"
#include "fs.h"
#include "stdio-kernel.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "string.h"
#include "interrupt.h"
#include "super_block.h"

struct dir root_dir;    // 根目录

// 打开根目录
void open_root_dir(struct partition* part) {
    root_dir.inode = inode_open(part, part->sb->root_inode_no);
    root_dir.dir_pos = 0;// 记录 在目录内的偏移地址， 目录指向自己 即偏移为 0
}

// 在分区 part 上打开 inode_no 编号的目录，并返回目录地址 
struct dir* dir_open(struct partition* part, uint32_t inode_no) {
    // 操作与 打开根目录大致一样， 但是需要为父目录分配内存
    // 因此 根目录是全局变量， 已经占有地址
    struct dir* pdir = (struct dir*)sys_malloc(sizeof(struct dir));
    pdir->inode = inode_open(part, inode_no);
    pdir->dir_pos = 0;
    return pdir;
}

// 在 part 分区内的 pdir 目录内寻找名为 name 的文件或目录,
// 找到后返回 true 并将其目录项存入 dir_e ,否则返回 false 
bool search_dir_entry(struct partition* part, struct dir* pdir, \
                const char* name, struct dir_entry* dir_e) {
    uint32_t block_cnt = 140;// 表示inode 的所有块 12 + 128

    // 分配内存, 方便检索此 inode 的全部扇区地址
    uint32_t* all_blocks = (uint32_t*)sys_malloc(48 + 512);
    if(all_blocks == NULL) {
        printk("search_dir_entry: sys_malloc for all_blocks failed!");
        return false;
    }
    uint32_t block_idx = 0;
    while (block_idx < 12) {// 先将目录中的直接索引录入到 all_blocks 中 
        all_blocks[block_idx] = pdir->inode->i_sectors[block_idx];
        block_idx++;
    }

    if(pdir->inode->i_sectors[12] != 0) { // 若存在一级索引块
        ide_read(part->my_disk, pdir->inode->i_sectors[12], all_blocks + 12, 1);
    }// 至此 all_blocks 中存放所有的 inode 信息

    // 写目录项时， 已经保证目录项不跨扇区
    // 这样 读目录项比较容易处理， 只申请容纳 1 个扇区的内存
    uint8_t* buf = (uint8_t*)sys_malloc(SECTOR_SIZE);
    // 用 p_de 来遍历 buf
    // p_de为指向目录项的指针,值为buf起始地址
    struct dir_entry* p_de = (struct dir_entry*)buf;

    uint32_t dir_entry_size = part->sb->dir_entry_size;
    uint32_t dir_entry_cnt = SECTOR_SIZE / dir_entry_size;// 一扇区可以容纳的目录项个数

    // 现在开始在所有块中 查找 目录项
    while(block_idx < block_cnt) {
        // 当 块地址 为 0 时，表示块中无数据， 继续找
        if(all_blocks[block_idx] == 0) {
            block_idx++;
            continue;
        }
        ide_read(part->my_disk, all_blocks[block_idx], buf, 1);// 继续读出下一个

        // 现在开始遍历扇区中 的所有目录项
        uint32_t dir_entry_idx = 0;// dir_entry_cnt 表示一个扇区最大容纳目录项数
        while (dir_entry_idx < dir_entry_cnt) {
            // 若找到了 便复制该目录项 (strcmp 相等时返回0
            if(!strcmp(p_de->filename, name)) {
                memcpy(dir_e, p_de, dir_entry_size);
                sys_free(buf);// 释放缓冲区
                sys_free(all_blocks);//释放 存放 inode 信息区
                return true;
            }
            dir_entry_idx++;
            p_de++;
        }

        block_idx++;
        p_de = (struct dir_entry*)buf;// buf 之后读入下一个块
        memset(buf, 0, SECTOR_SIZE);
    }
    sys_free(buf);// 释放缓冲区
    sys_free(all_blocks);//释放 存放 inode 信息区
    return false;
}


// 关闭目录————本质为 关闭 目录的inode ， 并释放目录占用的内存
void dir_close(struct dir* dir) {
    /*************      根目录不能关闭     ***************
     *1 根目录自打开后就不应该关闭,否则还需要再次open_root_dir();
     *2 root_dir所在的内存是低端1M之内,并非在堆中,free会出问题 */
    if(dir == &root_dir) {
        // 直接 返回，不用处理
        return;
    }
    inode_close(dir->inode);
    sys_free(dir);
}


//  在内存中初始化目录项p_de
void create_dir_entry(char* filename, uint32_t inode_no, uint8_t file_type, struct dir_entry* p_de) {
    ASSERT(strlen(filename) <= MAX_FILE_NAME_LEN);
    // 初始化目录项
    memcpy(p_de->filename, filename, strlen(filename));
    p_de->i_no = inode_no;
    p_de->f_type = file_type;
}



bool sync_dir_entry(struct dir* parent_dir, struct dir_entry* p_de, void* io_buf) {
    
}