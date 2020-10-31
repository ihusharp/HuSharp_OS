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

// 调用 ide.c 中的 分区列表
extern struct list partition_list;
// 默认情况下操作的分区
struct partition* cur_part;

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
        ide_read(hd, sb_buf->inode_bitmap_lba, cur_part->inode_bitmap.bits, sb_buf->block_bitmap_sects);

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
    p_de++;

    // sb.data_start_lba已经分配给了根目录,里面是根目录的目录项
    ide_write(hd, sb.data_start_lba, buf, 1);

    // over!
    printk("   root_dir_lba:0x%x\n", sb.data_start_lba);
    printk("%s format done\n", part->name);
    sys_free(buf);
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
}