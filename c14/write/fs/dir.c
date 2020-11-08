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
extern struct partition* cur_part;

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
    block_idx = 0;
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


// 将目录项 p_de 写入父目录 parent_dir 中,io_buf由主调函数提供
bool sync_dir_entry(struct dir* parent_dir, struct dir_entry* p_de, void* io_buf) {
    struct inode* dir_inode = parent_dir->inode;// 获取当前 inode
    uint32_t dir_size = dir_inode->i_size;//若此inode是目录,i_size是指该目录下所有目录项大小之和
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;// 目录项大小

    ASSERT(dir_size % dir_entry_size == 0);// 确保目录项不会跨扇区

    // 扇区所能容纳个数
    uint32_t dir_entrys_per_sec = (512 / dir_entry_size);

    // 将该目录的所有扇区地址(12个直接块+ 128个间接块)存入all_blocks
    uint8_t block_idx = 0;
    uint32_t all_blocks[140] = {0};// 用 all_blocks保存目录所有的块

    // 将 12 个直接块存入到 all_blocks 中
    while(block_idx < 12) {
        all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
        block_idx++;
    }

    // dir_e用来在io_buf中遍历目录项
    struct dir_entry* dir_e = (struct dir_entry*)io_buf;
    int32_t block_bitmap_idx = -1;

    /* 开始遍历所有块以寻找目录项空位,若已有扇区中没有空闲位,
     * 在不超过文件大小的情况下申请新扇区来存储新目录项 */
    block_idx = 0;
    int32_t block_lba;
    while(block_idx < 140) {
        // 必须从0全部遍历，
        // 若是采用从上一个位置开始遍历，可能之前的被删了
        block_bitmap_idx = -1;
        if(all_blocks[block_idx] == 0) {//若此扇区还未分配
            block_lba = block_bitmap_alloc(cur_part);
            if (block_lba == -1) {
                printk("alloc block bitmap for sync_dir_entry failed at alloc block! \n");
                return false;
            }

            // 每分配一个块， 就需要同步到位图中
            block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
            ASSERT(block_bitmap_idx != -1);
            // 同步到硬盘中
            bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

            block_bitmap_idx = -1;
            if(block_idx < 12) {// 若为直接块
                dir_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;
            } else if(block_idx == 12) {// 若为一级间接表块（刚好为 12 说明还未分配）
                dir_inode->i_sectors[12] = block_lba;// 首先将刚刚分配的块给一级表所指向的块
                block_lba = -1;
                block_lba = block_bitmap_alloc(cur_part);// 再分配一个块，作为第 0 个一级索引块
                if(block_lba == -1) {//说明分配失败
                    // 进行回滚，将之前同步到硬盘中的信息进行回滚
                    block_bitmap_idx = dir_inode->i_sectors[12] - cur_part->sb->data_start_lba;
                    bitmap_set(&cur_part->block_bitmap, block_bitmap_idx, 0);
                    dir_inode->i_sectors[12] = 0;
                    printk("alloc block bitmap for sync_dir_entry failed at alloc block_idx:12! \n");
                    return false;
                }
                // 每分配一个块就同步一次block_bitmap
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                ASSERT(block_bitmap_idx != -1);
                // 同步到硬盘中
                bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

                // 把新分配的第0个间接块地址写入一级间接块表
                all_blocks[12] = block_lba;
                ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks+12, 1);
            } else {//此时表示 block_idx 超过 12 ，那么 是需要分配间接索引块中的块
                all_blocks[block_idx] = block_lba;
                ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks+12, 1);
            }

            // 再将新目录写入到新分配的间接块中
            memset(io_buf, 0, 512);
            memset(io_buf, p_de, dir_entry_size);
            ide_write(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
            // 将 dir_inode 的 大小更新
            dir_inode->i_size += dir_entry_size;
            return true;
        }

        // 若第block_idx块已存在, 不需要分配， 将其读进内存,然后在该块中查找空目录项 
        ide_read(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
        // 在该扇区中找空闲目录项
        uint8_t dir_entry_idx = 0;
        while(dir_entry_idx < dir_entrys_per_sec) {
            if((dir_e + dir_entry_idx)->f_type == HS_FT_UNKNOWN) {
                memcpy(dir_e + dir_entry_idx, p_de, dir_entry_size);
                ide_write(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);

                dir_inode->i_size += dir_entry_size;
                return true;
            }
            dir_entry_idx++;// 下一个目录
        }
        block_idx++;// 下一个块
    }
    // 至此 没有 return true， 那么说明全部满了
    printk("directory is full!\n");
    return false;
}