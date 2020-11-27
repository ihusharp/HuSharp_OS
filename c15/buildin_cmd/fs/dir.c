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

// 把分区 part 目录 pdir 中编号为 inode_no 的目录项删除
bool delete_dir_entry(struct partition* part, struct dir* pdir, uint32_t inode_no, void* io_buf) {
    struct inode* dir_inode = pdir->inode;
    uint32_t block_idx = 0, all_blocks[140] = {0}, block_cnt = 12;
    // 收集目录全部地址
    // 1、 先存入 12 个直接块
    while(block_idx < 12) {
        all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
        block_idx++;
    }
    if(dir_inode->i_sectors[12] != 0) {
        block_cnt = 140;
        ide_read(part->my_disk, dir_inode->i_sectors[12], all_blocks+12, 1);
    }

    // 目录项在存储时保证不会跨扇区
    uint32_t dir_entry_size = part->sb->dir_entry_size;
    uint32_t dir_entry_idx;// 指示当前目录项 idx 
    uint32_t dir_entrys_per_sec = (SECTOR_SIZE / dir_entry_size); // 每扇区最大的目录项数目
    struct dir_entry* dir_e = (struct dir_entry*)io_buf;// 保存缓冲区
    bool is_dir_first_block = false;// 判断是否为 目录的第一块
    uint32_t dir_entry_cnt = 0;// 统计目录项个数 
    struct dir_entry* dir_entry_found = NULL;// 表示是否找到

    // 遍历所有块， 找到目录项
    block_idx = 0;
    while(block_idx < block_cnt) {// 若存在 间接块，那么为 140
        is_dir_first_block = false;
        
        if(all_blocks[block_idx] == 0) {
            block_idx++;
            continue;
        }
        // 说明此时含有数据
        dir_entry_idx = dir_entry_cnt = 0;
        memset(io_buf, 0, SECTOR_SIZE);
        // 读取扇区， 获得目录项
        ide_read(part->my_disk, all_blocks[block_idx], io_buf, 1);

        while(dir_entry_idx < dir_entrys_per_sec) {
            if((dir_e + dir_entry_idx)->f_type != HS_FT_UNKNOWN) {
                if(!strcmp((dir_e + dir_entry_idx)->filename, ".")) {
                    is_dir_first_block = true;
                } else if(strcmp((dir_e + dir_entry_idx)->filename, ".") && strcmp((dir_e + dir_entry_idx)->filename, "..") ) {
                    // 统计 . 和 .. 之外的所有目录项个数
                    dir_entry_cnt++;
                    if((dir_e + dir_entry_idx)->i_no == inode_no) {//说明找到了
                        ASSERT(dir_entry_found == NULL); // 确保目录中只有一个
                        dir_entry_found = dir_e + dir_entry_idx;
                        // 找到之后也要继续遍历， 以统计全部目录项个数
                    }
                }
            }
            dir_entry_idx++;
        }

        // 若此扇区没找到， 那么去下一个扇区
        if(dir_entry_found == NULL) {
            block_idx++;
            continue;
        }
        // 至此表示找到了，开始进行清理
        // 在此扇区中找到目录项后,清除该目录项并判断是否回收扇区,随后退出循环直接返回
        ASSERT(dir_entry_cnt >= 1);
        // 除目录第 1 个扇区外,若该扇区上只有该目录项自己,则将整个扇区回收
        if(dir_entry_cnt == 1 && !is_dir_first_block) {
            // 1、 首先在块位图中回收该块
            uint32_t block_bitmap_idx = all_blocks[block_idx] - part->sb->data_start_lba;
            bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
            bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

            // 2、 将该块地址从 数组 i_sectors 或者 索引表中去除
            // 需要判断是否在 一级索引表中
            if(block_idx < 12) {
                // 不在的话， 直接置为 0 即可
                dir_inode->i_sectors[block_idx] = 0;
            } else {//在一级间接索引表中擦除该间接块地址
                uint32_t indirect_blocks_cnt = 0;// 统计数量
                uint32_t indirect_blocks_idx = 12;
                while(indirect_blocks_idx < 140) {
                    if(all_blocks[indirect_blocks_idx] != 0) {
                        indirect_blocks_cnt++;
                    }
                }
                ASSERT(indirect_blocks_cnt >= 1);// 包括当前间接块

                // 将一级间接索引表上的 相应位置 清空， 再写入之前磁盘位置
                if(indirect_blocks_cnt > 1) {
                    all_blocks[block_idx] = 0;
                    ide_write(part->my_disk, dir_inode->i_sectors[12], all_blocks+12, 1);
                } else {//就当前一个间接块， 那么直接把间接索引表所在的块回收,然后擦除间接索引表块地址
                    block_bitmap_idx = dir_inode->i_sectors[12] - part->sb->data_start_lba;
                    // 将索引表置为 0
                    bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
                    bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
                }
                // 将间接索引表地址清 0
                dir_inode->i_sectors[12] = 0;
            }
        } else {//说明不止一个目录项， 或者是第一个扇区
            // 那么仅将该目录项清空即可
            memset(dir_entry_found, 0, dir_entry_size);
            ide_write(part->my_disk, all_blocks[block_idx], io_buf, 1);
        }
        // 更新 inode 节点信息， 并同步到硬盘
        ASSERT(dir_inode->i_size >= dir_entry_size);
        dir_inode->i_size -= dir_entry_size;// 更新i_size 少一个目录项
        memset(io_buf, 0, SECTOR_SIZE * 2);// 继续找下一个
        inode_sync(part, dir_inode, io_buf);// 同步 inode

        // block_idx++;// 下一个块
        return true;
    }
    // 遍历完所有都未找到  若出现这种情况应该是serarch_file出错了
    return false;
}


// 读取目录， 成功则返回一个目录项， 失败则返回 NULL
struct dir_entry* dir_read(struct dir* dir) {
    struct dir_entry* dir_e = (struct dir_entry*)dir->dir_buf;
    struct inode* dir_inode = dir->inode;
    
    uint32_t all_blocks[140] = {0}, block_cnt = 12;
    uint32_t block_idx = 0;

    while(block_idx < 12) {//首先将 12 个直接块直接复制
        all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
        block_idx++;
    }

    if(dir_inode->i_sectors[12] != 0) {//说明存在一级间接表
        ide_read(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
        block_cnt = 140;
    }
    block_idx = 0;

    // 现在开始遍历目录所有的块, 在每个块中遍历每个目录项
    // 由于目录中不止一个目录项， 通过 dir 中的 dir_pos 指针指向当前目录项
    // 每次遍历一个后， 便进行 指向下一个
    // 又由于 目录中的文件可以删除， 那么就会留下 空块， 为确保遍历一遍
    // 就需要判断此时 pos 是否指向 目录中的偏移目录项值
    uint32_t cur_dir_entry_pos = 0;
    // 求出 1 扇区内可容纳的目录项个数
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;
    uint32_t dir_entrys_per_sec = SECTOR_SIZE / dir_entry_size; 
    while(block_idx < block_cnt) {
        // sys_opendir 调用 dir_open 将  dir->dir_pos  置为 0
        if(dir->dir_pos >= dir_inode->i_size) {//已经指向最后了
            return NULL;// 说明此时还没找到
        }
        if(all_blocks[block_idx] == 0) {
            block_idx++;
            continue;
        }
        // 读取该块 到 dir_e 上， 来进行目录项的读取
        memset(dir_e, 0, SECTOR_SIZE);
        ide_read(cur_part->my_disk, all_blocks[block_idx], dir_e, 1);
        uint32_t dir_entry_idx = 0;
        // 开始遍历块中目录项
        while(dir_entry_idx < dir_entrys_per_sec) {
            // 确保为 文件或目录
            if((dir_e + dir_entry_idx)->f_type == HS_FT_DIRECTORY || 
                (dir_e + dir_entry_idx)->f_type == HS_FT_REGULAR) {
                if (cur_dir_entry_pos < dir->dir_pos) {// 由于是从 0 开始找
                    cur_dir_entry_pos += dir_entry_size;// 指向下一个
                    dir_entry_idx++;
                    continue;
                }// 至此 已经到指定位置了
                ASSERT(cur_dir_entry_pos == dir->dir_pos);
                dir->dir_pos += dir_entry_size;// 更新 pos 指针， 即返回下一个目录地址
                return dir_e + dir_entry_idx;
            }
            dir_entry_idx++;// 块中下一个 目录项
        }
        block_idx++;// 下一个块
    }
    return NULL;// 说明没有了
}

// 判断目录是否为空
bool dir_is_empty(struct dir* dir) {
    struct inode* dir_inode = dir->inode;
    // 若只有 . 和 .. 便说明目录为空
    return (dir_inode->i_size == cur_part->sb->dir_entry_size * 2);
}

// 在父目录 parent_dir 中删除 child_dir
int32_t dir_remove(struct dir* parent_dir, struct dir* child_dir) {
    // 首先需要判断 要删除的 child 目录为空
    // rm -r 虽然能强制删除， 但是实则为 递归删除，
    // 因此 HuSharp_OS 还是只能采用删除空目录
    int32_t block_idx = 1;
    struct inode* child_dir_inode = child_dir->inode;
    while(block_idx < 13) {
        // 对每一个块都进行判断， 判断除第一块外都为 0
        ASSERT(child_dir_inode->i_sectors[block_idx] == 0);
        block_idx++;
    }
    void* io_buf = sys_malloc(SECTOR_SIZE * 2);
    if (io_buf == NULL) {
        printk("dir_remove: malloc for io_buf failed\n");
        return -1;
    }

    // 在父目录 parent_dir 中删除子目录 child_dir 对应的目录项
    delete_dir_entry(cur_part, parent_dir, child_dir_inode, io_buf);

    // 回收 inode 中 i_sectors[0] 所占用的扇区， 并同步 bitmap
    inode_release(cur_part, child_dir_inode->i_no);
    sys_free(io_buf);
    return 0;
}
