#ifndef __DEVICE_IDE_H
#define __DEVICE_IDE_H
#include "bitmap.h"
#include "stdint.h"
#include "sync.h"

// 分区表结构
struct partition {
    uint32_t start_lba;             // 起始扇区
    uint32_t sec_cnt;               // 扇区数
    struct disk* my_disk;           // 标记分区所属的硬盘
    struct list_elem part_tag;      // 用于分区汇总到队列中的标记
    char name[8];                   // 分区名 
    struct super_block* sb;         // 分区的超级块
    struct bitmap block_bitmap;     // 块位图
    struct bitmap inode_bitmap;     // inode 点位图
    struct list open_inodes;        // 本分区打开的 inode 节点队列
};

struct disk {
    char name[8];                       // 硬盘名，如 sda， sdb
    struct ide_channel* my_channel;     // 此块硬盘归属于哪个 ide 通道
    uint8_t dev_no;                     // 本硬盘是主 0 还是 从 1
    struct partition prim_parts[4];     // 主分区最多 4 个
    struct partition logic_parts[8];    // 逻辑分区最多 8 个
};

// ide 通道结构， 即 ata 通道
struct ide_channel { 
    char name[8];                   // 本 ata 通道名称
    uint16_t port_base;             // 本通道的起始端口号 主盘为 0x1F0 从盘为0x170
    // 硬盘的中断处理程序，需要根据中断号来选择通道号
    uint8_t irq_no;                 // 本通道所用中断号
    struct lock lock;               // 加上通道端口选择锁 一次只允许一个通道进行中断
    bool excepting_itr;             // 标记是否是正在等待的中断
    struct semaphore disk_done;            // 一个信号量
    struct disk devices[2];            // 一个通道中的两个硬盘
};

void intr_hd_handler(uint8_t irq_no);
void ide_init(void);
extern uint8_t channel_cnt;
extern struct ide_channel channels[];
void ide_read(struct disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt);
void ide_write(struct disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt);

#endif