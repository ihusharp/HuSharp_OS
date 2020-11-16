#ifndef __FS_SUPER_BLOCK_H
#define __FS_SUPER_BLOCK_H
#include "stdint.h"

// 超级块
// 占用一个扇区 即 512 字节
struct super_block {
    uint32_t magic;     // 用来标识文件系统类型
                        // 支持多文件系统的操作系统通过此标志来识别文件系统类型
    uint32_t sec_cnt;   // 本分区占用的扇区数
    uint32_t inode_cnt; // 本分区的 inode 数
    uint32_t part_lba_base; // 本分区的 lba 扇区地址

    uint32_t block_bitmap_lba;  // 空闲块位图的起始 扇区地址
    uint32_t block_bitmap_sects;// 空闲位图占用的扇区数量

    uint32_t inode_bitmap_lba;  // iNode节点的位图起始地址
    uint32_t inode_bitmap_sects;// iNode位图占用的扇区数量

    uint32_t inode_table_lba;   // iNode 数组的起始扇区 lba 地址
    uint32_t inode_table_sects; // iNode 数组占用的扇区数量

    uint32_t data_start_lba;    // 数据区的开始扇区号
    uint32_t root_inode_no;     // 根目录所在节点号
    uint32_t dir_entry_size;    // 目录项大小


    // 以上为 13 个变量，每个 4 字节，因此 共有 4 × 13 = 52 字节
    uint8_t fill[460];  // 因此需要填充 460 字节
} __attribute__ ((packed));



#endif