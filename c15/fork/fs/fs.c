#include "fs.h"
#include "debug.h"
#include "dir.h"
#include "global.h"
#include "ide.h"
#include "inode.h"
#include "list.h"
#include "memory.h"
#include "stdint.h"
#include "stdio-kernel.h"
#include "string.h"
#include "super_block.h"
#include "file.h"
#include "console.h"

// 调用 ide.c 中的 分区列表
extern struct list partition_list;
// 默认情况下操作的分区
struct partition* cur_part;
// 根目录
extern struct dir root_dir; 


// 在分区链表中找到名为 part_name 的分区,并将其指针赋值给 cur_part 
// pelem 用于分区汇总到队列中的标记
// arg 为待对比参数，此处为 分区名
static bool mount_partition(struct list_elem* pelem, int arg) {
    char* part_name = (char*)arg;// 将 arg 还原为 字符指针
    // 由 list 标记得到 标记所属的 partition 
    struct partition* part = elem2entry(struct partition, part_tag, pelem);
    // 判断是否为 所需要的 分区
    if(!strcmp(part->name, part_name)) {// 相等时返回0，说明找到了该分区
        cur_part = part;// 找到了默认的分区 
        // 获得分区所在硬盘
        struct disk* hd = cur_part->my_disk;

        // sb_buf 用于存储从硬盘读入的超级块
        struct super_block* sb_buf = (struct super_block*)sys_malloc(SECTOR_SIZE);

        // 在内存中创建 cur_part 的超级块
        cur_part->sb = (struct super_block*)sys_malloc(sizeof(struct super_block));
        if (cur_part->sb == NULL) {
            PANIC("alloc super_block failed!");
        }

        // 读入超级块到 缓存中
        memset(sb_buf, 0, SECTOR_SIZE);
        ide_read(hd, cur_part->start_lba + 1, sb_buf, 1);// 读入一扇区

        // 将 缓冲区 sb_buf 超级块的信息复制到分区的超级块 sb 中
        // 占位的 pad[460] 不用移植
        memcpy(cur_part->sb, sb_buf, sizeof(struct super_block));

        /**********     将硬盘上的块位图读入到内存    ****************/
        cur_part->block_bitmap.bits = (uint8_t*)sys_malloc(sb_buf->block_bitmap_sects * SECTOR_SIZE);
        if(cur_part->block_bitmap.bits == NULL) {
            PANIC("alloc block_bitmap failed!");
        }

        cur_part->block_bitmap.btmp_bytes_len = sb_buf->block_bitmap_sects * SECTOR_SIZE;
        // 从硬盘上读入块位图到分区的block_bitmap.bits
        ide_read(hd, sb_buf->block_bitmap_lba, cur_part->block_bitmap.bits, sb_buf->block_bitmap_sects);

        /**********     将硬盘上的inode位图读入到内存    ************/
        cur_part->inode_bitmap.bits = (uint8_t*)sys_malloc(sb_buf->inode_bitmap_sects * SECTOR_SIZE);
        if(cur_part->inode_bitmap.bits == NULL) {
            PANIC("alloc inode_bitmap failed!");
        }
        cur_part->inode_bitmap.btmp_bytes_len = sb_buf->inode_bitmap_sects * SECTOR_SIZE;
        // 从硬盘上读入块位图到分区的block_bitmap.bits
        ide_read(hd, sb_buf->inode_bitmap_lba, cur_part->inode_bitmap.bits, sb_buf->inode_bitmap_sects);

        list_init(&cur_part->open_inodes);
        printk("mount %s done!\n", part->name);

        /* 此处返回true是为了迎合主调函数list_traversal的实现,与函数本身功能无关。
        只有返回true时list_traversal才会停止遍历,减少了后面元素无意义的遍历.*/
        return true;
    }
    return false; // 使list_traversal继续遍历
}

// 格式化分区，即初始化分区的元信息
static void partition_format(struct partition* part) {
    // 为方便实现，块 与 扇区 同 大小
    uint32_t boot_sector_sects = 1; // 引导块大小为 1 扇区
    uint32_t super_block_sects = 1; // 超级块占用扇区数
    /*******   首先计算元信息所需要的扇区数及位置   ************/
    // I结点位图占用的扇区数.最多支持4096个文件
    // 即 inode 位图为 1 扇区 4096 / 4096
    uint32_t inode_bitmap_sects = DIV_ROUND_UP(MAX_FILES_PER_PART, BITS_PER_SECTOR);	   
    // inode 数组占用的扇区数， 由 inode 的尺寸和数量决定
    uint32_t inode_table_sects = DIV_ROUND_UP(((sizeof(struct inode) * MAX_FILES_PER_PART)), SECTOR_SIZE);
    // 空闲块位图
    uint32_t used_sects = boot_sector_sects + super_block_sects + inode_bitmap_sects + inode_table_sects;
    uint32_t free_sects = part->sec_cnt - used_sects;// 空闲块数量

    // 现在得到空闲位图占据扇区数
    uint32_t block_bitmap_sects;
    block_bitmap_sects = DIV_ROUND_UP(free_sects, BITS_PER_SECTOR);// BITS_PER_SECTOR = 4096
    // 空闲位图占用部分之前空闲的空间，需要减去后更新
    uint32_t block_bitmap_bit_len;     // 位图中位的长度,也是可用块的数量
    block_bitmap_bit_len = free_sects - block_bitmap_sects; // 直接从上面算的空闲块减去, 得到真正的可用块数量
    block_bitmap_sects = DIV_ROUND_UP(block_bitmap_bit_len, BITS_PER_SECTOR);

    /*******   其次在内存中创建超级块，将计算出的元信息写入到超级块   ************/
    // 超级块的初始化
    struct super_block sb;// 此时运用的是 栈中的内存，512 字节 够用
    sb.magic = 0x20000712;
    sb.sec_cnt = part->sec_cnt;
    sb.inode_cnt = MAX_FILES_PER_PART;
    sb.part_lba_base = part->start_lba;
    sb.block_bitmap_lba = sb.part_lba_base + 2; // 第0块是引导块,第1块是超级块
    sb.block_bitmap_sects = block_bitmap_sects;

    sb.inode_bitmap_lba = sb.block_bitmap_lba + sb.block_bitmap_sects;// inode 位图在block 位图后面
    sb.inode_bitmap_sects = inode_bitmap_sects;

    sb.inode_table_lba = sb.inode_bitmap_lba + sb.inode_bitmap_sects;
    sb.inode_table_sects = inode_table_sects;

    sb.data_start_lba = sb.inode_table_lba + sb.inode_table_sects;
    sb.root_inode_no = 0;   // 将 第 0 个 inode 给 根目录
    sb.dir_entry_size = sizeof(struct dir_entry);

    printk("%s info:\n", part->name);
    printk("   magic:0x%x\n   part_lba_base:0x%x\n   all_sectors:0x%x\n   inode_cnt:0x%x\n   block_bitmap_lba:0x%x\n   block_bitmap_sectors:0x%x\n   inode_bitmap_lba:0x%x\n   inode_bitmap_sectors:0x%x\n   inode_table_lba:0x%x\n   inode_table_sectors:0x%x\n   data_start_lba:0x%x\n", sb.magic, sb.part_lba_base, sb.sec_cnt, sb.inode_cnt, sb.block_bitmap_lba, sb.block_bitmap_sects, sb.inode_bitmap_lba, sb.inode_bitmap_sects, sb.inode_table_lba, sb.inode_table_sects, sb.data_start_lba);

    /*******   将超级块写入到磁盘  ************/
    struct disk* hd = part->my_disk;
    // 1、将超级块写入本分区的 1 扇区
    ide_write(hd, part->start_lba + 1, &sb, 1);
    printk("    super_block_lba:0x%x\n", part->start_lba + 1);

    /*******   将元信息写入到磁盘 ************/
    // 找出数据量中最大的元信息，使其尺寸作为 存储缓冲区
    uint32_t buf_size = (sb.block_bitmap_sects >= sb.inode_bitmap_sects ? sb.block_bitmap_sects : sb.inode_bitmap_sects);
    buf_size = (buf_size >= sb.inode_table_sects ? buf_size : sb.inode_table_sects) * SECTOR_SIZE;
    uint8_t* buf = (uint8_t*)sys_malloc(buf_size);  // 申请的内存 由 内存管理系统清 0 后返回

    // 2、将块位图初始化 并写入到 block_bitmap_lba
    // 初始化
    buf[0] |= 0x01; // 第 0 个预留给 root 目录，因此先占位
    uint32_t block_bitmap_last_byte = block_bitmap_bit_len / 8;
    uint8_t block_bitmap_last_bit = block_bitmap_bit_len % 8;
    // last_size是位图所在最后一个扇区中，不足一扇区的其余部分
    uint32_t last_size = SECTOR_SIZE - (block_bitmap_last_byte % SECTOR_SIZE);

    // 1、 先将 位图最后一字节到其所在扇区置为 1 ，
    // 表示超出实际块数的部分置为 已占用，不要 在碰它
    memset(&buf[block_bitmap_last_byte], 0xff, last_size);

    // 2、 再将上一步中 覆盖的最后一字节内的有效位 重新置 0
    uint8_t bit_idx = 0;
    while (bit_idx <= block_bitmap_last_bit) {
        buf[block_bitmap_last_byte] &= ~(1 << bit_idx++);
    }
    ide_write(hd, sb.block_bitmap_lba, buf, sb.block_bitmap_sects);

    // 3、将 inode 位图初始化并写入到 sb.inode_bitmap_lba
    // 先清空缓冲区
    memset(buf, 0, buf_size);
    buf[0] |= 0x1;
    /* 由于inode_table中共4096个inode,位图inode_bitmap正好占用1扇区,
    * 即inode_bitmap_sects等于1, 所以位图中的位全都代表inode_table中的inode,
    * 无须再像block_bitmap那样单独处理最后一扇区的剩余部分,
    * inode_bitmap所在的扇区中没有多余的无效位 */
    ide_write(hd, sb.inode_bitmap_lba, buf, sb.inode_bitmap_sects);

    // 4、将inode数组初始化并写入sb.inode_table_lba
    // 由于 inode 数组是由 inode_bitmap 决定，因此不需要对inode 是否越界进行考虑
    // 只要考虑 inode_bitmap 是否越界
    memset(buf, 0, buf_size);
    struct inode* i = (struct inode*)buf;
    // 现在初始化根目录信息
    i->i_size = sb.dir_entry_size * 2;//首先为 根目录分配 . 和 .. 
    i->i_no = 0;    // 第 0 个inode 为 root
    i->i_sectors[0] = sb.data_start_lba;// 将根目录安排在最开始的空闲块     
    // 由于上面的 memset,i_sectors 数组的其它元素都初始化为0 
    ide_write(hd, sb.inode_table_lba, buf, sb.inode_table_sects);

    /*******   将 root 初始化后写入到磁盘 ************/
    // 写入 . 和 ..
    memset(buf, 0, buf_size);
    struct dir_entry* p_de = (struct dir_entry*)buf;

    // 1、写入 .
    memcpy(p_de->filename, ".", 1);
    p_de->i_no = 0;  
    p_de->f_type = HS_FT_DIRECTORY;
    p_de++;

    // 2、写入 ..
    memcpy(p_de->filename, "..", 2);
    p_de->i_no = 0; // 根目录的父目录依然是根目录自己
    p_de->f_type = HS_FT_DIRECTORY;
    // p_de++;

    // sb.data_start_lba已经分配给了根目录,里面是根目录的目录项
    ide_write(hd, sb.data_start_lba, buf, 1);

    // over!
    printk("   root_dir_lba:0x%x\n", sb.data_start_lba);
    printk("%s format done\n", part->name);
    sys_free(buf);
}


// 对路径进行解析
// name_store 为主调函数提供的缓冲区 ， 用于存储最上层路径名
// eg 解析 /a/b/c  namestore 存储 "a"  返回 /b/c
// 将最上层路径解析出来， 存储到 name_store 中
static char* path_parse(char* pathname, char* name_store) {

    if(pathname[0] == '/') {//说明为根目录, 而根目录不需要解析，因为已经打开了
        // 如果遇到类似 ///a/b, 那么跳过//
        while(*(++pathname) == '/');
    }
    // 开始普通的路径解析
    while(*pathname != '/' && *pathname != 0) {
        *name_store++ = *pathname++;// 存储
    }
    
    if(pathname[0] == 0) {//路径字符串为空, 说明开始就为空 或者指向 '/0'
        return NULL;
    }

    return pathname;// 返回分解后的子路径
}

// 返回路径深度， /a/a/b 深度为 3
// 由于 存在 //a//b//c 的形式， 因此不能通过 / 个数来判断深度
// 因此循环调用 path_parse 进行解析 
int32_t path_depth_cnt(char* pathname) {
    ASSERT(pathname != NULL);
    char* p = pathname;// 保存该变量
    char name[MAX_FILE_NAME_LEN];// 用于保存 path_parse 的参数
    uint32_t depth = 0;// 作为循环 path_parse 的路径深度保存

    p = path_parse(p, name);
    while(name[0]) {// 还未到 '/0'
        depth++;
        memset(name, 0, MAX_FILE_NAME_LEN);
        if(p) {//p 分解后的子路径
            p = path_parse(p, name);// 继续分析
        }
    }

    return depth;
}

// 所有调用 search_file 函数的主调函数， 都应该释放 目录 parent_dir 以免内存泄漏
// 至于为什么需要 parent_dir 是因为主调函数经常需要 搜索文件的 parent 目录来进行操作, 比如create时， 需要知道是哪个父目录 
// 搜索文件pathname,若找到则返回其inode号,否则返回-1
// pathname 是全路径 
static int search_file(const char* pathname, struct path_search_record* searched_record) {
    // 如果待查找的是根目录， 那么 为了避免以下的无效查找， 直接返回根目录信息即可
    if(!strcmp(pathname, "/") || !strcmp(pathname, "/.") ||!strcmp(pathname, "/..")) {
        searched_record->file_type = HS_FT_DIRECTORY;
        searched_record->parent_dir = &root_dir;
        searched_record->searched_path[0] = 0;// 搜索路径置为 空
        return 0;
    }

    uint32_t path_len = strlen(pathname);
    ASSERT(pathname[0] == '/' && path_len > 1 && path_len < MAX_PATH_LEN);
    char* sub_path = (char*)pathname;// 用 sub_path 代指 进行操作

    struct dir* parent_dir = &root_dir;// 从根目录往下找
    struct dir_entry dir_e;// 查找各个文件

    // 记录路径解析出来的各级名称， 如路径 "/a/b/c"
    // 数组 name 每次的值分别为 "a"、“b”、 “c”
    char name[MAX_FILE_NAME_LEN] = {0};

    searched_record->file_type = HS_FT_UNKNOWN;
    searched_record->parent_dir = parent_dir;

    // 当前文件父目录的inode号
    uint32_t parent_inode_no = 0;// 由于当前为 根节点，其父目录为根目录 inode = 0

    sub_path = path_parse(sub_path, name);// 至此 已经剥去了最上层路径
    // 搜索文件的原理为： 每解析出一层路径名， 就去相应目录中确认目录项
    // 将其 与 目录项中的 filename 进行对比， 找到后继续进行解析
    // 直到找到所有目录， 或者找不到时停止
    while(name[0]) {// 只要不为 '/0', 那么就继续
        // 用 searched_path 记录所有经过的 路径
        ASSERT(strlen(searched_record->searched_path) < 512);

        // 记录已经存在的父目录
        // 追加到 记录路径中
        strcat(searched_record->searched_path, "/");// 最开始为 根目录， 之后为 父目录的分隔符
        strcat(searched_record->searched_path, name);

        // 在所给的目录中查找文件
        // dir_e 存储所找到的目录信息
        if(search_dir_entry(cur_part, parent_dir, name, &dir_e)) {
            memset(name, 0, MAX_FILE_NAME_LEN);// 将name 清 0 ，因为之后还要用
            if(sub_path) {//不为 null， 说明还存在子路径
                sub_path = path_parse(sub_path, name);// 再取
            }

            if(dir_e.f_type == HS_FT_DIRECTORY) {//打开为 目录
                parent_inode_no = parent_dir->inode->i_no;// 备份父目录编号
                dir_close(parent_dir);// 根目录不会被关闭， 在 dir_close 函数中直接返回
                parent_dir = dir_open(cur_part, dir_e.i_no);// 更新父目录为当前目录
                searched_record->parent_dir = parent_dir;
                continue;
            } else if(dir_e.f_type == HS_FT_REGULAR) {// 若为普通文件
                searched_record->file_type = HS_FT_REGULAR;
                return dir_e.i_no;
            }
        } else {// 若没有找到 ，则返回 -1
                /* 找不到目录项时,要留着parent_dir不要关闭,
                 * 若是创建新文件的话需要在 parent_dir 中创建 */
                // eg 见 sys_open（其为search 的主调函数之一， 会进行关闭） 
            return -1;
        }
    }

    // 执行到此, 必然是
    // 1、遍历了完整路径并且查找的文件 
    // 2、最后一层不是普通文件， 而是同名目录
    dir_close(searched_record->parent_dir);

    // 保存被查找目录的直接父目录 
    // 假设 /a/b/c  经过之前步骤， 现在 parent_dir 必然存的是 c
    // 但是 我们需要的 c 尽管不是我们要的文件， 其 parent_dir 仍应该为 b
    searched_record->parent_dir = dir_open(cur_part, parent_inode_no);
    searched_record->file_type = HS_FT_DIRECTORY;
    return dir_e.i_no;

}

// 打开或创建文件成功后， 返回文件描述符， 否则返回 -1
// 返回文件描述符 即返回 fd_table 中的下标
// flags 格式为 eg  O_RDONLY | O_WRONLY | O_RDWR | O_CREAT
/* 
enum oflags {
   O_RDONLY,	  // 只读
   O_WRONLY,	  // 只写
   O_RDWR,	  // 读写
   O_CREAT = 4	  // 创建
}; */
int32_t sys_open(const char* pathname, uint8_t flags) {
    // 由于该函数目前只支持 文件 的打开， 不支持目录的打开
    // 因此 需要判断 是否为 目录
    // 目录 以 '/' 结尾
    if(pathname[strlen(pathname) - 1] == '/') {//说明为目录
        printk("can't open a directory_%s by sys_open!\n", pathname);
        return -1;
    }

    // 最大为 O_RDONLY | O_WRONLY | O_RDWR | O_CREAT
    ASSERT(flags <= 7);// 限制 flags 格式

    int32_t fd = -1;// 默认为找不到

    // 路径搜索函数， 当失败时， 可以知道是在哪一个路径出错
    // eg 若查找目标文件 c ，其绝对路径为 a/b/c ，
    //      若 b 不存在， 那么 searched_record 存入 /a/b
    //      若 成功， 便存入 /a/b/c
    // 若是创建文件， 可以知道创建文件父目录
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));

    // 记录目录深度， 
    uint32_t pathname_depth = path_depth_cnt((char*)pathname);

    // 首先检查文件是否存在
    int32_t inode_no = search_file(pathname, &searched_record);
    bool found = (inode_no != -1) ? true : false;

    // 再文件判断为 目录 还是文件
    if(searched_record.file_type == HS_FT_DIRECTORY) {
        printk("can't open a directory_%s by sys_open!\n", pathname);
        dir_close(searched_record.parent_dir);// 这就是之前说的 search  378 行
        return -1;
    }

    uint32_t path_search_depth = path_depth_cnt(searched_record.searched_path);
    // 再对访问路径进行比对, 若深度不一致， 那么说明访问到中间失败
    if(pathname_depth != path_search_depth) {
        printk("can't access %s: Not a directory, subpath %s isn't exit!\n", 
                    pathname, searched_record.searched_path);
        dir_close(searched_record.parent_dir);
        return -1;
    }// 至此， 已经找到最后路径

    // 若在最后一个路径没有找到， 且不是要创建文件, 是想要打开文件， 直接返回 -1
    if(!found && !(flags & OP_CREAT)) {// 没找到， 且 不是 create
        printk("in sys_open: in path %s, file %s isn't exist!\n", 
        searched_record.searched_path,
        (strrchr(searched_record.searched_path, '/') + 1));
        // strrchr 从右往左查找字符串str中首次出现字符ch的地址(不是下标,是地址)
        // 并不是输出 pathname ，是因为可能是在中间某个 路径 失败
        dir_close(searched_record.parent_dir);
        return -1;
    } else if(found && (flags & OP_CREAT)) {//说明想要创建的文件已经存在了！
        printk("%s has already exist!", pathname);
        dir_close(searched_record.parent_dir);
        return -1;    
    }

    switch (flags & OP_CREAT)
    {
    case OP_CREAT:
        printk("creating file...\n");
        fd = file_create(searched_record.parent_dir, (strrchr(pathname, '/') + 1), flags);
        dir_close(searched_record.parent_dir);
        break;
    
    default:// 其他情况均为打开已经存在文件
        fd = file_open(inode_no, flags);
        break;
    }
    return fd;

}

// 将文件描述符转化为文件表的下标 
static uint32_t fd_local2global(uint32_t local_fd) {
    struct task_struct* cur = running_thread();
    int32_t global_fd = cur->fd_table[local_fd];
    ASSERT(global_fd >= 0 && global_fd < MAX_FILE_OPEN);
    return (uint32_t)global_fd;
}

// 关闭文件描述符 fd 指向的文件 成功就返回 0， 否则 返回 -1
int32_t sys_close(int32_t fd) {
    int32_t ret = -1;
    if(fd > 2) {
        uint32_t fd_idx = fd_local2global(fd);// 首先得到在文件表中的下标
        ret = file_close(&file_table[fd_idx]);
        running_thread()->fd_table[fd_idx] = -1;//表示此文件描述符可用
    }
    return ret;
}

/* 标准输入输出描述符
enum std_fd {
    stdin_no,   // 0 标准输入
    stdout_no,  // 1 标准输出
    stderr_no   // 2 标准错误
};*/
// 将 buf 中连续 count 个字节写入文件描述符 fd 中， 成功则返回写入字节数， 失败返回 -1
int32_t sys_write(int32_t fd, const void* buf, uint32_t count) {
    if(fd < 0) {
        printk("sys_write: fd error!\n");
        return -1;
    }
    if(fd == stdout_no) {// 对标准输出进行处理
        // 说明此时是往 屏幕 中打印信息
        char tmp_buf[1024] = {0};
        memcpy(tmp_buf, buf, count);
        console_put_str(tmp_buf);
        return count;
    }
    uint32_t _fd = fd_local2global(fd);// 得到文件表的下标 _fd
    struct file* wr_file = &file_table[_fd];
    if(wr_file->fd_flag & OP_WONLY || wr_file->fd_flag & OP_RDWR) {
        // 值得注意的是：单纯的 OP_CREATE 只能创建文件 不能写入文件
        uint32_t bytes_written = file_write(wr_file, buf, count);
        return bytes_written;
    } else {
        console_put_str("sys_write: not allowed to write file without flag O_RDWR or O_WRONLY!\n");
        return -1;
    }

}


// 从文件描述符 fd 指向的文件中读取 count 字节到 buf 中
// 成功则返回读取字节数、 否则返回 -1
int32_t sys_read(int32_t fd, void* buf, uint32_t count) {
    if(fd < 0 ) {
        printk("sys_read: fd_error");
        return -1;
    }
    ASSERT(buf != NULL);
    uint32_t _fd = fd_local2global(fd);// 得到文件描述符中的下标
    return file_read(&file_table[_fd], buf, count);
}


// 提供不关闭文件就能自由更改读写位置的方法
// 重置用于文件读写操作的偏移指针， 成功时返回新的偏移值,失败返回 -1
// 文件读写位置由 fd_pos 决定
// sys_lseek 的原理是将 whence + offset  转换为 fd_pos
int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence) {
    if(fd < 0) {
        printk("sys_lseek : fd_error\n");
        return -1;
    }

    ASSERT(whence > 0 && whence < 4);
    // 转换为文件表下标
    uint32_t _fd = fd_local2global(fd);
    struct file* pf = &file_table[_fd];

    int32_t file_size = (int32_t)pf->fd_inode->i_size;
    int32_t new_pos = 0;// 新的偏移量必须位于文件大小之内
    switch(whence) {
        case SEEK_SET:
            new_pos = offset;
            break;

        case SEEK_CUR:
            new_pos = (int32_t)pf->fd_pos + offset;
            break;
        case SEEK_END:
            new_pos = file_size + offset;
    }
    if(new_pos < 0 || new_pos > (file_size - 1)) {
        return -1;
    }
    pf->fd_pos = new_pos;
    return pf->fd_pos;
}

// 删除文件(非目录),成功返回0,失败返回-1
// unlink 删除文件
// rmdir 删除目录
int32_t sys_unlink(const char* pathname) {
    ASSERT(strlen(pathname) < MAX_PATH_LEN);

    // 先检查待删除文件是否存在
    struct path_search_record search_record;
    memset(&search_record, 0, sizeof(struct path_search_record));
    // 找到则返回其inode号,否则返回-1
    int inode_no = search_file(pathname, &search_record);
    ASSERT(inode_no != 0);
    if(inode_no == -1) {// 没找到
        printk("file %s not found!\n", pathname);
        dir_close(search_record.parent_dir);
        return -1;
    }
    // 至此 表示找到该同名 inode （可能是目录
    if(search_record.file_type == HS_FT_DIRECTORY) {
        printk("can't delete a directory with unlink!, please use rmdir() to instead!\n");
        dir_close(search_record.parent_dir);
    }

    // 至此 表示找到该文件  但还需要判断是否此时被打开
    // 即判断是否位于文件表中
    uint32_t file_idx = 0;
    while(file_idx < MAX_FILE_OPEN) {
        if(file_table[file_idx].fd_inode != NULL && (uint32_t)inode_no == file_table[file_idx].fd_inode->i_no) {//表示找到了
            break;
        }
        file_idx++;
    }
    if(file_idx < MAX_FILE_OPEN) {//说明找到了 而非遍历到结尾
    // 那么此时必须要关闭删除操作， 因为还在使用
        dir_close(search_record.parent_dir);
        printk("file %s is in using! not allow to delete now!\n", pathname);
        return -1;
    }
    ASSERT(file_idx == MAX_FILE_OPEN);// 至此 必须是遍历完所有打开文件， 且没有找到
    // 那么可以开始删除了
    // 1、 先分配缓冲区， 缓冲区大小为 2 个块
    void* io_buf = sys_malloc(SECTOR_SIZE * 2);
    if (io_buf == NULL) {
        dir_close(search_record.parent_dir);
        printk("sys_unlink: malloc for io_buf failed\n");
    return -1;
    }
    // 开始删除
    struct dir* parent_dir = search_record.parent_dir;
    delete_dir_entry(cur_part, parent_dir, inode_no, io_buf);// 删除目录项
    inode_release(cur_part, inode_no);// 删除 inode
    sys_free(io_buf);// 释放
    dir_close(search_record.parent_dir);
    return 0;
}


// 创建目录 pathname ,成功返回 0 ,失败返回 -1
int32_t sys_mkdir(const char* pathname) {
    uint8_t rollback_step = 0;  // 用于操作失败的回滚操作

    void* io_buf = sys_malloc(SECTOR_SIZE * 2);
    if (io_buf == NULL) {
        printk("sys_mkdir: sys_malloc for io_buf failed\n");
        return -1;
    }

    // 1、首先需要判断 同名文件 是否存在
    struct path_search_record search_record;
    memset(&search_record, 0, sizeof(struct path_search_record));
    int inode_no = -1;
    inode_no = search_file(pathname, &search_record);
    if(inode_no != -1) {// 说明找到同名文件
        printk("sys_mkdir: file of directory %s exist!\n", pathname);
        rollback_step = 1;// 回滚
        goto rollback;
    } else {// 若未找到,也要判断是在最终目录没找到还是某个中间目录不存在
    // 比如 /a/b 可能没有 /a/ 那么也就会找不到同名，即中间目录不存在
        uint32_t pathname_depth = path_depth_cnt((char*)pathname);
        uint32_t path_searched_depth = path_depth_cnt(search_record.searched_path);

        // 先判断是否 depth 一致， 如果一致， 那么说明都访问到了
        if(pathname_depth != path_searched_depth) {
            // search_record 变量 searched_path 记录访问过的 存在的 目录
            printk("sys_mkdir: can't access %s, subpath %s isn't exist!\n", pathname, search_record.searched_path);
            rollback_step = 1;
            goto rollback;
        }
    }
    // 至此 说明可以进行创建了
    // 主要分为以下几个步骤：(1 为 判断是否有同名文件)
    // 2、为新目录创建 inode
    struct dir* parent_dir = search_record.parent_dir;
    // 目录名称后可能会有字符'/',所以最好直接用s earched_record.searched_path,无'/'
    // eg 若 想创建 /a/b ， 那么 dirname 指向 b  
    char* dirname = strrchr(search_record.searched_path, '/') + 1;

    inode_no = inode_bitmap_alloc(cur_part);
    if (inode_no == -1) {
        printk("sys_mkdir: allocate inode failed\n");
        rollback_step = 1;
        goto rollback;
    }

    struct inode new_dir_inode;
    inode_init(inode_no, &new_dir_inode);// 进行初始化

    uint32_t block_bitmap_idx = 0;// 用来 记录 block 对应于 block_bitmap 中的位置
    int32_t block_lba = -1;
    // 3、为目录分配一个块
    block_lba = block_bitmap_alloc(cur_part);
    if(block_lba == -1) {
        printk("sys_mkdir: block_bitmap_alloc for create directory failed\n");
        rollback_step = 2;
        goto rollback;
    }
    new_dir_inode.i_sectors[0] = block_lba;
    // 每分配一个块， 就同步
    block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
    ASSERT(block_bitmap_idx != 0);
    bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

    // 4、 写入 '.' 、 '..'
    memset(io_buf, 0, SECTOR_SIZE * 2);// 清空 io_buf 
    struct dir_entry* p_de = (struct dir_entry*)io_buf;

    // 4.1、 写入'.'
    memcpy(p_de->filename, ".", 1);
    p_de->i_no = inode_no;
    p_de->f_type = HS_FT_DIRECTORY;

    p_de++;
    // 4.2、 写入 '..'
    memcpy(p_de->filename, "..", 2);
    p_de->i_no = parent_dir->inode->i_no;
    p_de->f_type = HS_FT_DIRECTORY;

    ide_write(cur_part->my_disk, new_dir_inode.i_sectors[0], io_buf, 1);
    // 初始化目录尺寸为 2 个目录项大小
    new_dir_inode.i_size = 2 * cur_part->sb->dir_entry_size;

    // 5、在父目录中添加自己的目录项
    struct dir_entry new_dir_entry;
    memset(&new_dir_entry, 0, sizeof(struct dir_entry));

    create_dir_entry(dirname, inode_no, HS_FT_DIRECTORY, &new_dir_entry);

    memset(io_buf, 0, SECTOR_SIZE * 2);	 // 清空io_buf
    if (!sync_dir_entry(parent_dir, &new_dir_entry, io_buf)) {	  // sync_dir_entry中将block_bitmap通过bitmap_sync同步到硬盘
        printk("sys_mkdir: sync_dir_entry to disk failed!\n");
        rollback_step = 2;
        goto rollback;
    }

    // 6、 元信息的持久化
    // 父目录 inode 同步到硬盘
    memset(io_buf, 0, SECTOR_SIZE * 2);
    inode_sync(cur_part, parent_dir->inode, io_buf);

    // 新目录 inode 同步到 硬盘
    memset(io_buf, 0, SECTOR_SIZE * 2);
    inode_sync(cur_part, &new_dir_inode, io_buf);

    // 将 inode 位图同步到 硬盘
    bitmap_sync(cur_part, inode_no, INODE_BITMAP);

    sys_free(io_buf);

    // 关闭 所创建目录的父目录
    dir_close(search_record.parent_dir);
    return 0;

// 创建文件或目录需要创建相关的多个资源,若某步失败则会执行到下面的回滚步骤 
// 确保原子性
rollback:
    switch(rollback_step) {
        case 2:// 如果新文件的inode创建失败,之前位图中分配的inode_no也要恢复 
            bitmap_set(&cur_part->inode_bitmap, inode_no, 0);// 不用同步， 因为完成同步是在确保正确时
        case 1:// 关闭创建的父目录
            dir_close(search_record.parent_dir);
            break;
    }

    sys_free(io_buf);
    return -1;// 至此说明没找到
}

// 读取目录中所有的目录项， 在遍历之前必须要先把目录打开， 然后再关闭
// 目录打开成功后， 返回目录指针， 失败返回 NULL 
struct dir* sys_opendir(const char* pathname) {
    ASSERT(strlen(pathname) < MAX_PATH_LEN);

    // 由于 根目录形式为 '/'  '/.'  '/..'
    // 如果是根目录 '/'， 那么直接返回 &root_dir 
    if(pathname[0] == '/' && (pathname[1] == 0 || pathname[0] == '.')) {
        return &root_dir;
    }

    // 先检查待打开的目录是否存在
    struct path_search_record search_record;
    memset(&search_record, 0, sizeof(struct path_search_record));
    int inode_no = search_file(pathname, &search_record);

    struct dir* ret = NULL;
    if(inode_no == -1) {// 如果找不到目录， 那么提示不存在的路径
        printk("In %s, sub path %s not exist!\n", pathname, search_record.searched_path);
    } else {// 说明找到了
        if(search_record.file_type == HS_FT_REGULAR) {
            printk("%s is a regular file!\n", pathname);
        } else if(search_record.file_type == HS_FT_DIRECTORY){
            ret = dir_open(cur_part, inode_no);
        }
    }
    dir_close(search_record.parent_dir);
    return ret;
}

// 成功关闭目录 dir 返回 0 ,失败返回 -1
int32_t sys_closedir(struct dir* dir) {
    int32_t ret = -1;
    if(dir != NULL) {
        dir_close(dir);
        ret = 0;
    }
    return ret;
}

// 读取目录dir的1个目录项,成功后返回其目录项地址,到目录尾时或出错时返回NULL
struct dir_entry* sys_readdir(struct dir* dir) {
    ASSERT(dir != NULL);
    return dir_read(dir);
}

// 目录回绕  把目录dir的指针dir_pos置0
// 与 lseek 相似， 目录也经常需要回到 pos 为 0 处
void sys_rewinddir(struct dir* dir) {
    dir->dir_pos = 0;
}



// 删除空目录,成功时返回 0 ,失败时返回 -1
int32_t sys_rmdir(const char* pathname) {
    // 先检查待删除文件是否存在
    struct path_search_record search_record;
    memset(&search_record, 0, sizeof(struct path_search_record));
    int inode_no = search_file(pathname, &search_record);
    ASSERT(inode_no != 0);// 确保已经找到，肯定不会删除 根目录
    int ret = -1;// 默认返回值失败
    if(inode_no == -1) {
        printk("In %s, sub path %s not exist\n", pathname, search_record.searched_path); 
    } else {
        if (search_record.file_type == HS_FT_REGULAR) {
            printk("%s is regular file!\n", pathname);
        } else {
            struct dir* dir = dir_open(cur_part, inode_no);
            if(!dir_is_empty(dir)) {// 非空目录不可删除
                printk("dir %s is not empty, it is not allowed to delete a nonempty directory!\n", pathname);
            } else {//说明此时为目录， 且不为空， 那么可以删除
                if(!dir_remove(search_record.parent_dir, dir)) {
                    ret = 0;
                }
            }
            // 之前打开了目录， 需要关闭
            dir_close(dir);
        }
        dir_close(search_record.parent_dir);
        return ret;
    }
}


// 获取 child_inode_nr 的 父目录的 inode 
// child_inode_nr 为 子目录 的 inode 编号
static uint32_t get_parent_dir_inode_nr(uint32_t child_inode_nr, void* io_buf) {
    // 先打开 子目录'..' 的 inode
    // 因为 '..' 的 目录项中包含 父目录 '.' 的inode 
    struct inode* child_dir_inode = inode_open(cur_part, child_inode_nr);

    uint32_t block_lba = child_dir_inode->i_sectors[0];

    ASSERT(block_lba >= cur_part->sb->data_start_lba);
    inode_close(child_dir_inode);
    // 读入到 io_buf 中
    ide_read(cur_part->my_disk, block_lba, io_buf, 1);
    // 取 '..'   , 第0个目录项是".",第1个目录项是".."
    struct dir_entry* dir_e = (struct dir_entry*)io_buf;

    ASSERT(dir_e[1].i_no < 4096 && dir_e[1].f_type == HS_FT_DIRECTORY);
    return dir_e[1].i_no;
}


// 在 inode 编号 为 p_inode_nr 中， 查找 inode 编号为 c_inode_nr 的子目录名字
// 将名字存入缓冲区 path ， 成功则返回 0， 否则返回 -1
static int get_child_dir_name(uint32_t p_inode_nr, uint32_t c_inode_nr, char* path, void* io_buf) {
    struct inode* parent_dir_inode = inode_open(cur_part, p_inode_nr);

    // 填充 all_blocks ， 将该目录的所占扇区地址写入到 all_blocks 中
    uint32_t all_blocks[140] = {0};
    uint8_t block_idx = 0;
    
    while(block_idx < 12) {
        all_blocks[block_idx] = parent_dir_inode->i_sectors[block_idx];
        block_idx++;
    }
    uint32_t block_cnt = 12;
    if(parent_dir_inode->i_sectors[12]) {// 若包含， 就直接写入
        ide_read(cur_part->my_disk, parent_dir_inode->i_sectors[12], all_blocks + 12, 1);
        block_cnt = 140;
    }
    inode_close(parent_dir_inode);

    struct dir_entry* dir_e = (struct dir_entry*)io_buf;
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;// size
    uint32_t dir_entrys_per_sec = (512 / dir_entry_size);// 最大块个数
    block_idx = 0;
    // 遍历所有块
    while(block_idx < block_cnt) {
        if(all_blocks[block_idx] != 0) {// 相应块不为 空 则读入相应块
            ide_read(cur_part->my_disk, all_blocks[block_idx],io_buf, 1);
            uint8_t dir_e_idx = 0;

            // 遍历每个目录项
            while(dir_e_idx < dir_entrys_per_sec) {
                if((dir_e + dir_e_idx)->i_no == c_inode_nr) {
                    // 说明找到了
                    strcat(path, "/");
                    strcat(path, (dir_e + dir_e_idx)->filename);
                    return 0;
                }
                dir_e_idx++;
            }
        }
        block_idx++;
    }
    return -1;
}


// 将当前工作目录绝对路径写入到 buf 中， size 是 buf 的大小
char* sys_getcwd(char* buf, uint32_t size) {
    ASSERT(buf != NULL);// 确保不为空
    // 若为空， 就系统分配一个
    void* io_buf = sys_malloc(SECTOR_SIZE);
    if(io_buf == NULL) {
        printk("sys_getcwd: sys_malloc failed!\n");
        return NULL;
    }

    // 获取当前任务工作目录的 inode
    struct task_struct* cur_thread = running_thread();
    int32_t child_inode_nr = cur_thread->cwd_inode_nr;
    ASSERT(child_inode_nr >= 0 && child_inode_nr < 4096);

    // 若当前目录为 根目录， 直接返回 '/'
    if(child_inode_nr == 0) {
        buf[0] = '/';
        buf[1] = 0;// 结尾
        return buf;
    }   

    memset(buf, 0, size);
    // 对路径进行反转， 比如 'ab/c' 反转为 'c/ab', 
    char full_path_reverse[MAX_PATH_LEN] = {0};

    /* 从下往上逐层找父目录,直到找到根目录为止.
     * 当child_inode_nr为根目录的inode编号(0)时停止,
     * 即已经查看完根目录中的目录项 */
    // 进行递归查找
    int32_t parent_inode_nr = 0;
    while(child_inode_nr != 0) {//直至找到 根目录
    // 值得说明的是， 由于是从底向上拼接， 因此目录是反转拼接
    // 'ab/c/d' 拼接为 'd/c/ab'
        parent_inode_nr = get_parent_dir_inode_nr(child_inode_nr, io_buf);
        if(get_child_dir_name(parent_inode_nr, child_inode_nr, full_path_reverse, io_buf) == -1) {
            // 当没有找到时
            sys_free(io_buf);
            return NULL;
        }
        child_inode_nr = parent_inode_nr;
    }
    ASSERT(strlen(full_path_reverse) <= size);// 确保名字小于 buf

    // 现将 full_path_reverse 路径反转
    char* last_slash;   // 用于记录最后一个斜杠地址
    // 从右往左查找
    while( (last_slash = strrchr(full_path_reverse, '/')) != 0) {
        uint16_t len = strlen(buf);
        strcpy(buf + len, last_slash);// 正向存储到 io_buf 中
        // 在 full_path_reverse 中添加结束字符,作为下一次执行 strcpy 中 last_slash 的边界
        *last_slash = 0; // 结束字符
    }
    sys_free(io_buf);
    return buf;
}


// 更改当前工作目录为绝对路径 path,成功则返回 0 ,失败返回 -1
// 目标就是修改 pcb 中的 cwd_inode_nr  
int32_t sys_chdir(const char* path) {
    // 更改工作目录， 首先要确保该目录存在
    int32_t ret = -1;// 默认返回地址
    struct path_search_record search_record;
    memset(&search_record, 0, sizeof(struct path_search_record));
    int inode_no = search_file(path, &search_record);
    if(inode_no != -1) {
        if(search_record.file_type == HS_FT_DIRECTORY) {
            running_thread()->cwd_inode_nr = inode_no;// 进行更新
            ret = 0;// 说明找到了
        } else {
            printk("sys_chdir: %s is regular file or other!\n", path);
        } 
    } 
    dir_close(search_record.parent_dir);
    return ret;
}

// 在 buf 中填充文件结构的相关信息， 成功则返回 0 ， 否则返回 1
int32_t sys_stat(const char* path, struct stat* buf) {  
    // 若直接查看根目录
    if(!strcmp(path, "/") || !strcmp(path, "/.") || !strcmp(path, "/..")) {
        buf->st_filetype = HS_FT_DIRECTORY;
        buf->st_ino = 0;
        buf->st_size = root_dir.inode->i_size;
        return 0;
    }


    struct path_search_record search_record;
    memset(&search_record, 0, sizeof(struct path_search_record));
    // 现在开始查找 inode ， 要是找到， 便进行文件属性填充， 否则返回 -1
    int32_t ret = -1;   // 首先默认返回值

    uint32_t inode_no = search_file(path, &search_record);
    if(inode_no != -1) {
         // 只为获得文件大小
        struct inode* obj_inode = inode_open(cur_part, inode_no);
        buf->st_size = obj_inode->i_size;
        inode_close(obj_inode);
        buf->st_filetype = search_record.file_type;
        buf->st_ino = inode_no;
    } else {
        printk("sys_stat: %s not found!\n", path);
    }
    dir_close(search_record.parent_dir);
    return ret;
}



// 在磁盘上搜索文件系统,若没有则格式化分区创建文件系统 
void filesys_init() {
    // 通过三重循环，遍历各个通道中各个硬盘中的各个分区，来创建/找到文件系统
    uint8_t channel_no = 0, dev_no, part_idx = 0;
    
    // sb_buf用来存储从硬盘上读入的超级块
    struct super_block* sb_buf = (struct super_block*)sys_malloc(SECTOR_SIZE);

    if (sb_buf == NULL) {
        PANIC("alloc memory failed! ");
    }
    printk("Searching FileSystem.......\n");

    // 目前只支持 partition_format 创建的文件系统, 即魔数为 0x20000712
    // 遍历通道
    while (channel_no < channel_cnt) {
        dev_no = 0;// 从第 0 盘开始
        // 遍历通道内的硬盘
        while (dev_no < 2) {
            if (dev_no == 0) {//跨过 hd60M.img
                dev_no++;
                continue;
            }
            struct disk* hd = &channels[channel_no].devices[dev_no];
            struct partition* part = hd->prim_parts;// 指向主分区
            while (part_idx < 12) {// 最多包括 4 个 主分区 + 8 个逻辑分区
                if(part_idx == 4) {// 至此 开始处理逻辑分区
                    part = hd->logic_parts;
                }

            // 首先虽然最大为 12 个分区，但不一定都能用到，因此需要判断是否存在
            /* channels数组是全局变量,默认值为0,disk属于其嵌套结构,
	        * partition又为disk的嵌套结构,因此partition中的成员默认也为0.
	        * 若partition未初始化,则partition中的成员仍为0.
            * 因此 此处可以用 part 中的 sec_cnt 是否为 0 判断是否分区存在 
	        * 下面处理存在的分区. */
                if(part->sec_cnt != 0) {//如果分区判断存在
                    memset(sb_buf, 0, SECTOR_SIZE);// 首先进行初始化

                    // 读出分区的超级块,根据魔数是否正确来判断是否存在文件系统
                    ide_read(hd, part->start_lba + 1, sb_buf, 1);//第0块是引导块,第1块是超级块

                    // 判断是否存在 文件系统
                    if(sb_buf->magic == 0x20000712) {
                        printk("%s has FileSystem!\n", part->name);
                    } else {//此时需要创建
                        printk("formatting %s's partition %s .......\n", hd->name, part->name);
                        partition_format(part);
                    }
                } 
                // 至此，开始下一个分区的判断
                part_idx++;
                part++;
            }
            // 至此 硬盘中的分区已经遍历完毕
            dev_no++;
        }
        // 至此，通道中的硬盘已经遍历完毕
        channel_no++;
    }
    // 至此 均已经变了完毕，需要释放
    sys_free(sb_buf);

    // 确定默认操作的分区
    char default_part[8] = "sdb1";

    // 利用 list_traversal 对每个分区进行挂载
    // 遍历列表的所有元素，判断是否有 elem 满足条件
    // 判断方法采用 func 回调函数进行判断
    list_traversal(&partition_list, mount_partition, (int)default_part);// 回调函数
    

    // 将当前分区的根目录打开
    open_root_dir(cur_part);

    // 初始化文件表
    uint32_t fd_idx = 0;
    while(fd_idx < MAX_FILE_OPEN) {
        file_table[fd_idx++].fd_inode = NULL; 
    }

}

