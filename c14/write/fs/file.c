#include "file.h"
#include "debug.h"
#include "fs.h"
#include "global.h"
#include "inode.h"
#include "interrupt.h"
#include "memory.h"
#include "stdio-kernel.h"
#include "string.h"
#include "super_block.h"
#include "thread.h"

#define MAX_FILES_OPEN_PER_PROC 8

// 文件表
// 表示的是将文件打开的次数，而非文件个数
// 因为文件可以多次打开，也会占表
struct file file_table[MAX_FILE_OPEN];

// 从文件表file_table中获取一个空闲位,成功返回下标,失败返回-1
int32_t get_free_slot_in_global(void)
{
    uint32_t fd_idx = 3; // 跨过 stdin、 stdout、 stderr
    while (fd_idx < MAX_FILE_OPEN) {
        if (file_table[fd_idx].fd_inode == NULL) {
            // break;// 说明找到了
            return fd_idx;
        }
        fd_idx++;
    } // 至此 说明还未找到
    if (fd_idx == MAX_FILE_OPEN) {
        printk("exceed max open files!\n");
        return -1;
    } else {
        printk("the error fd_idx is %d", &fd_idx);
    }
    return fd_idx;
}

// globa_fd_idx 表示 全局描述符 的 下标, 也就是 数组 file_table 的下标
// 将全局描述符下标安装到进程或线程自己的文件描述符数组fd_table中,
// 成功返回下标, 失败返回-1
int32_t pcb_fd_install(int32_t globa_fd_idx)
{
    struct task_struct* cur = running_thread();
    uint8_t local_fd_idx = 3; // 跨过 stdin、 stdout、 stderr
    while (local_fd_idx < MAX_FILES_OPEN_PER_PROC) {
        if (cur->fd_table[local_fd_idx] == -1) { // -1 表示 free_slot ，可用
            cur->fd_table[local_fd_idx] = globa_fd_idx;
            break;
        }
        local_fd_idx++;
    }
    if (local_fd_idx == MAX_FILES_OPEN_PER_PROC) {
        printk("exceed max open files_per_proc\n");
        return -1;
    }
    return local_fd_idx;
}

//  分配一个i结点,返回i结点号
int32_t inode_bitmap_alloc(struct partition* part)
{
    int32_t bit_idx = bitmap_scan(&part->inode_bitmap, 1);
    if (bit_idx == -1) {
        return -1;
    }
    bitmap_set(&part->inode_bitmap, bit_idx, 1);
    return bit_idx;
}

// 分配一个扇区， 返回其扇区地址
int32_t block_bitmap_alloc(struct partition* part)
{
    int32_t bit_idx = bitmap_scan(&part->block_bitmap, 1);
    if (bit_idx == -1) {
        return -1;
    }
    bitmap_set(&part->block_bitmap, bit_idx, 1);
    // 和inode_bitmap_malloc不同,此处返回的不是位图索引,而是具体可用的扇区地址
    return (part->sb->data_start_lba + bit_idx);
}

// 将内存中 bitmap 第 bit_idx 位所在的 512 字节同步到硬盘
// 可以为 inode 位图， 也可以为 block 位图
void bitmap_sync(struct partition* part, uint32_t bit_idx, uint8_t btmp_type)
{
    uint32_t off_sec = bit_idx / 4096; // 位图中 bit_idx 位置， 以 512bytes = 4096bits 为单位，得到 第几个扇区
    uint32_t off_size = off_sec * BLOCK_SIZE; // 得到 字节地址
    uint32_t sec_lba;
    uint8_t* bitmap_off;

    // 需要被同步到硬盘的位图只有 inode_bitmap 和 block_bitmap
    switch (btmp_type) {
    case INODE_BITMAP:
        sec_lba = part->sb->inode_bitmap_lba + off_sec; // 位图扇区地址
        bitmap_off = part->inode_bitmap.bits + off_size; // bit 为单位
        break;
    case BLOCK_BITMAP:
        sec_lba = part->sb->block_bitmap_lba + off_sec;
        bitmap_off = part->block_bitmap.bits + off_size;
        break;
    default:
        break;
    }
    ide_write(part->my_disk, sec_lba, bitmap_off, 1);
} // 写入一个扇区

// 创建文件， 若成功则返回文件描述符 ， 否则返回 -1
// 在 parent_dir 中， 以 flag 格式创建 filename
int32_t file_create(struct dir* parent_dir, char* filename, uint8_t flag)
{
    // 1、 分配 inode， inode_bitmap 、inode_cnt
    // 2、

    // 后续操作的公共缓冲区
    // 由于 将其写入硬盘是必须的 也是 最后一步
    // 因此最好一开始就分配好内存， 以免最后内存不够导致之前步骤白费
    // 一般读写是一个扇区， 为防止跨扇区， 因此申请 2 个扇区
    void* io_buf = sys_malloc(1024);
    if (io_buf == NULL) {
        printk("in file_create: sys_malloc for io_buf failed!\n");
        return -1;
    }

    uint8_t rollback_step = 0; // 记录回滚各资源状态

    // 创建文件 inode -> 文件描述符 fd -> 目录项 -> 写入硬盘

    // 首先创办 inode
    int32_t inode_no = inode_bitmap_alloc(cur_part);
    if (inode_no == -1) {
        printk("in file_create: inode_allocing failed!\n");
        return -1;
    }

    // 此 inode 应该在堆中申请， 不得由局部变量得到（局部变量会在函数退出时释放
    struct inode* new_file_inode = (struct inode*)sys_malloc(sizeof(struct inode*));
    if (new_file_inode == NULL) {
        printk("in file_create: sys_malloc for inode_allocing failed!\n");
        // 此时 由于改变了 inode_bitmap 因此需要回滚
        rollback_step = 1;
        goto rollback;
    } // 至此 表示创建成功
    inode_init(inode_no, new_file_inode); // init

    // 写入到 fd
    int32_t fd_idx = get_free_slot_in_global();
    if (fd_idx == -1) {
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
    if (!sync_dir_entry(parent_dir, &new_dir_entry, io_buf)) {
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
    new_file_inode->i_open_cnts = 1; // 将该文件被打开数 置为 1

    sys_free(io_buf); // 至此 已经同步完所有步骤
    return pcb_fd_install(fd_idx);

rollback:
    switch (rollback_step) // 值得注意的是， 由于此处的 几个情况回滚是循序渐进的， 因此不采用 break
    {
    case 3: // 表示 目录项 写入到 父目录中失败， 应当将 写入进 file_table 中的 fd 回滚
        memset(&file_table[fd_idx], 0, sizeof(struct file));
    case 2: // 表示 fd 分配超过 PCB 的最大文件数
        sys_free(new_file_inode);
    case 1: // inode 节点创建失败， 需要回滚之前 inode_bitmap 中分配的 新inode
        bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
        break;
    default:
        break;
    }
}

// 打开编号为 inode_no 对应文件， 若成功则返回文件描述符， 否则返回 -1
int32_t file_open(uint32_t inode_no, uint8_t flag)
{
    uint32_t fd_idx = get_free_slot_in_global(); // 获取文件表中空闲位置

    if (fd_idx == -1) {
        printk("file_open: exceed max open files!\n");
        return -1;
    }
    // 进行初始化
    file_table[fd_idx].fd_inode = inode_open(cur_part, inode_no);
    file_table[fd_idx].fd_pos = 0; //每次打开文件时， pos 都应该指向 0
    file_table[fd_idx].fd_flag = flag; // // 文件操作标识, 判断是否读写
    bool* write_deny = &file_table[fd_idx].fd_inode->write_deny;

    if (flag & OP_WONLY || flag & OP_RDWR) { // 只要可以写， 就判断是否有其他文件在写
        // 先关中断
        enum intr_status old_status = intr_disable();
        if (!(*write_deny)) { // 此时没有其他文件写
            *write_deny = true;
            intr_set_status(old_status);
        } else { //说明已经有在写的
            intr_set_status(old_status);
            printk("file_open: file can't be write now, try again later!\n");
            return -1;
        }
    } // 若是读文件或者创建文件， 那么就不用管 write_deny
    return pcb_fd_install(fd_idx); // 安装到 文件表中
}

// 关闭文件
int32_t file_close(struct file* file)
{
    if (file == NULL) {
        return -1;
    }
    // 成功了， 就恢复inode 状态
    file->fd_inode->write_deny = false;
    inode_close(file->fd_inode);
    file->fd_inode = NULL;
    return 0;
}

// 把 buf 中的 count 个字节写入 file,成功则返回写入的字节数,失败则返回-1
int32_t file_write(struct file* file, const void* buf, uint32_t count)
{
    // 首先加入字节后， 会不会超过文件最大尺寸 12+128
    if ((file->fd_inode->i_size + count) > (BLOCK_SIZE * 140)) {
        printk("file_write: exceed max file_size! write size failed!\n");
        return -1;
    }

    // 现在开始通过缓冲区写入, 单位为 1 扇区
    uint8_t* io_buf = sys_malloc(BLOCK_SIZE);
    if (io_buf == NULL) {
        printk("file_write: sys_malloc for io_buf failed\n");
        return -1;
    }

    // 用来记录文件所有的块地址
    uint32_t* all_blocks = (uint32_t*)sys_malloc(BLOCK_SIZE + 48);
    if (all_blocks == NULL) {
        printk("file_write: sys_malloc for all_blocks failed\n");
        return -1;
    }

    const uint8_t* src = buf; // 用src指向buf中待写入的数据
    uint32_t bytes_written = 0; // 用来记录已写入数据大小
    uint32_t size_left = count; // 用来记录未写入数据大小
    int32_t block_lba = -1; // 块地址
    uint32_t block_bitmap_idx = 0; // 用来记录block对应于block_bitmap中的索引,做为参数传给bitmap_sync
    uint32_t sec_idx; // 用来索引扇区
    uint32_t sec_lba; // 扇区地址
    uint32_t sec_off_bytes; // 扇区内字节偏移量
    uint32_t sec_left_bytes; // 扇区内剩余字节量
    uint32_t chunk_size; // 每次写入硬盘的数据块大小
    int32_t indirect_block_table; // 用来获取一级间接表地址
    uint32_t block_idx; // 块索引

    // 判断是否是第一次写入， 若是的话， 首先分配块
    if(file->fd_inode->i_sectors[0] == 0) {
        block_lba = block_bitmap_alloc(cur_part);
        if(block_lba == -1) {//说明分配失败
            printk("file_write: block_bitmap_alloc failed!\n");
            return -1;
        }
        file->fd_inode->i_sectors[0] = block_lba;

        // 将位图同步到硬盘中
        block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
        ASSERT(block_bitmap_idx != 0);
        bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
    }

    // 现在需要写入 count 字节， 因此需要判断此时的 扇区块是否够用， 是否需要分配新块
    uint32_t file_has_used_blocks = file->fd_inode->i_size / BLOCK_SIZE + 1;
    // 为什么要 +1 ，比如 只用一个扇区， 那么由于 取模， 会=0

    // 写入 count 字节后， 所占用的块数
    uint32_t file_will_use_blocks = (file->fd_inode->i_size + count) / BLOCK_SIZE + 1;
    ASSERT(file_will_use_blocks <= 140);

    // 通过此增量判断是否需要分配， 为 0 表示够用
    uint32_t add_blocks = file_will_use_blocks - file_has_used_blocks;

    // 通过 all_blocks 收集原块和 count 需要的块地址
    if(add_blocks == 0) {//表示目前扇区够用
        if(file_has_used_blocks <= 12) {
            block_idx = file_has_used_blocks - 1;//指向最后一个扇区
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
        }
    } else {//说明需要分配新的块地址 涉及到分配新扇区及是否分配一级间接块表,下面要分三种情况处理
        // 1、 12 个直接块够用
        if(file_will_use_blocks <= 12) {
            // 先将剩余可用的扇区地址写入到 all_blocks 中
            block_idx = file_has_used_blocks - 1;//指向最后一个扇区
            ASSERT(file->fd_inode->i_sectors[block_idx] != 0);
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];

            // 再将未来可用的扇区分配好之后 写入到 all_blocks 中
            block_idx = file_has_used_blocks;// 指向第一个要分配的新扇区
            while(block_idx < file_will_use_blocks) {
                block_lba = block_bitmap_alloc(cur_part);
                if (block_lba == -1) {
                    printk("file_write: block_bitmap_alloc for situation 1 failed\n");
                    return -1;
                }

                // 写文件时,不应该存在块未使用但已经分配扇区的情况,当文件删除时,就会把块地址清0
                // 因此需要先 进行判断
                ASSERT(file->fd_inode->i_sectors[block_idx] == 0);
                // 然后再进行写入
                file->fd_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;

                // 每分配一个块， 就需要同步到 硬盘中
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

                block_idx++;// 下一个
            }
        } else if(file_has_used_blocks <= 12 && file_will_use_blocks > 12) {
            // 2、 分配的超过 直接块
            block_idx = file_has_used_blocks - 1;// 指向已经使用的最后一个扇区
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
        
            // 创建一级间接块
            block_lba = block_bitmap_alloc(cur_part);
            if(block_lba == -1) {
                printk("file_write: block_bitmap_alloc for situation 2 failed\n");
                return -1;
            }

            ASSERT(file->fd_inode->i_sectors[12] == 0); // 确保一级间接块表未分配
            // 分配一级间接块索引表
            indirect_block_table = file->fd_inode->i_sectors[12] = block_lba;

            block_idx = file_has_used_blocks;
            while(block_idx < file_will_use_blocks) {
                block_lba = block_bitmap_alloc(cur_part);
                if(block_lba == -1) {
                    printk("file_write: block_bitmap_alloc for situation 2 failed\n");
                    return -1;
                }

                // 对待首先在直接块上分配的扇区而言
                if(block_idx < 12) {
                    ASSERT(file->fd_inode->i_sectors[block_idx] == 0);// 确保还未使用
                    file->fd_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;
                } else {//对待间接块而言
                    all_blocks[block_idx] = block_lba;// 和字节写入到 all_blocks 中， 待全部分配完成后， 再同步到 硬盘中
                }

                // 每分配一个块， 就同步位图
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
            
                block_idx++;
            }
            // 再将 all_blocks 中的 一级间接块表 同步到硬盘
            ide_write(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        } else if(file_has_used_blocks > 12) {
            // 3、新数据只占据 间接块
            ASSERT(file->fd_inode->i_sectors[12] != 0); // 已经具备了一级间接块表
            // 获取间接索引块
            indirect_block_table = file->fd_inode->i_sectors[12];

            // 将 已经使用的间接块 也收录到 all_blocks 中
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);

            block_idx = file_has_used_blocks;// 第一个未使用的间接块
            while(block_idx < file_will_use_blocks) {
                block_lba = block_bitmap_alloc(cur_part);
                if (block_lba == -1) {
                    printk("file_write: block_bitmap_alloc for situation 3 failed\n");
                    return -1;
                }
                all_blocks[block_idx++] = block_lba;

                // 每分配一个块， 就同步位图
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
            }
            // 再将 all_blocks 中的 一级间接块表 同步到硬盘
            ide_write(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
    }// 至此 all_blocks 包括 已经使用的块地址 ， 以及新数据要占用的新地址
    bool first_write_block = true; // 标识本次写入操作中第一个需要写入的块
    // 由于写入该块时， 该块可能之前就含有数据， 因此需要先将其读出到缓冲区， 
    // 追加数据到缓冲区的空闲区域后， 再一起写入
    file->fd_pos = file->fd_inode->i_size - 1;// 置 fd_pos 为文件大小 -1
    while(bytes_written < count) {//直到写完所有数据
        memset(io_buf, 0, BLOCK_SIZE);
        sec_idx = file->fd_inode->i_size / BLOCK_SIZE;
        sec_lba = all_blocks[sec_idx];
        sec_off_bytes = file->fd_inode->i_size % BLOCK_SIZE;// 最后一个数据偏移量
        sec_left_bytes = BLOCK_SIZE - sec_off_bytes;// 块中剩余空间

        // 判断此次写入硬盘的数据大小
        // size_left 初始值为  count 用来记录未写入数据大小 
        // 当要写入的比当前块剩余空间小时， 那便直接写入， 否则， 先写入剩余空间大小数据 
        chunk_size = size_left < sec_left_bytes ? size_left : sec_left_bytes;// 每次写入硬盘的数据块大小
        // 若为第一个块， 即已经写过的最后一个块， 可能是含有数据的， 因此需要先读出来
        if(first_write_block) {
            ide_read(cur_part->my_disk, sec_lba, io_buf, 1);
            first_write_block = false;
        }
        memcpy(io_buf + sec_off_bytes, src, chunk_size);
        ide_write(cur_part->my_disk, sec_lba, io_buf, 1);
        printk("file write at lba 0x%x\n", sec_lba);

        src += chunk_size;  // 将指针推移到下一个数据
        file->fd_inode->i_size += chunk_size;   // 更新文件大小
        file->fd_pos += chunk_size;
        bytes_written += chunk_size;
        size_left -= chunk_size;
    }// 至此 已经写完所有数据到 硬盘 中
    // 现在需要更新 inode 值， 以及释放 all_blocks 和 io_buf
    inode_sync(cur_part, file->fd_inode, io_buf);
    sys_free(all_blocks);
    sys_free(io_buf);
    return bytes_written;
}
