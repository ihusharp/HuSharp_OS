#include "ide.h"
#include "sync.h"
#include "io.h"
#include "stdio.h"
#include "stdio-kernel.h"
#include "interrupt.h"
#include "memory.h"
#include "debug.h"
#include "console.h"
#include "timer.h"
#include "string.h"
#include "list.h"

// 定义硬盘各个寄存器的端口号
#define reg_data(channel)	    (channel->port_base + 0)
#define reg_error(channel)	    (channel->port_base + 1)
#define reg_sect_cnt(channel)	(channel->port_base + 2)
#define reg_lba_l(channel)	    (channel->port_base + 3)
#define reg_lba_m(channel)	    (channel->port_base + 4)
#define reg_lba_h(channel)	    (channel->port_base + 5)
#define reg_dev(channel)	    (channel->port_base + 6)
#define reg_status(channel)	    (channel->port_base + 7)
#define reg_cmd(channel)	    (reg_status(channel))
#define reg_alt_status(channel)  (channel->port_base + 0x206)
#define reg_ctl(channel)	    reg_alt_status(channel)

// reg_alt_status 寄存器的一些关键位
#define BIT_STAT_BSY    0x80    // 硬盘忙
#define BIT_STAT_DRDY   0x40    // 驱动器准备好
#define BIT_STAT_DRQ   0x8     // 数据传输准备好了

// device 寄存器的一些关键位
#define BIT_DEV_MBS     0xa0    // 第 7 位 和 第 5 位固定为 1
#define BIT_DEV_LBA     0x40    // device 的 LBA 位 选择启用 LBA方式
#define BIT_DEV_DEV     0x10

// 一些硬盘操作的指令
#define CMD_IDENTIFY        0xec    // 硬盘识别
#define CMD_READ_SECTOR     0x20    // 读扇区
#define CMD_WRITE_SECTOR    0x30    // 写扇区

// 定义可读写的最大扇区数， 调试用的 ，避免越界
#define max_lba ((80*1024*1024 / 512) - 1)    // 只支持 80 MB 硬盘

uint8_t channel_cnt;    // 按硬盘数计算的通道数
struct ide_channel channels[2]; // 两个 ide 通道

// 用于记录总扩展分区的起始lba,初始为0,
// partition_scan时以此为标记 发现为 0 时，便知道是第一次扫描
int32_t ext_lba_base = 0;

uint8_t p_no = 0, l_no = 0; // 用来记录硬盘主分区和逻辑分区的下标

struct list partition_list; //分区队列

// 分区表项结构体
// 16 字节大小
struct partition_table_entry {
    uint8_t bootable;   //是否可引导
    uint8_t start_head; //起始磁头号
    uint8_t start_sec;  // 起始柱面号
    uint8_t start_chs; // 起始柱面号
    uint8_t  fs_type;		 // 分区类型
    uint8_t  end_head;		 // 结束磁头号
    uint8_t  end_sec;		 // 结束扇区号
    uint8_t  end_chs;		 // 结束柱面号    

    uint32_t start_lba;		 // 本分区起始扇区的lba地址
    uint32_t sec_cnt;		 // 本分区的扇区数目
} __attribute__ ((packed));	 // 保证此结构是16字节大小
// 为编译器 gcc 提供的属性定义，防止为了对齐而压缩

// 引导扇区,mbr或ebr所在的扇区 
struct boot_sector {
    uint8_t other[446];     //引导代码，此处用于占位，以找到分区表
    struct partition_table_entry partition_table[4];    //分区表中有四项
    uint16_t signature;     // 启动扇区的结束标记为 0x55AA
} __attribute__ ((packed));	


// 选择读写的硬盘 选出是主盘还是从片，
// 通过 device 寄存器的 dev 位进行选择
static void select_disk(struct disk* hd) {
    uint8_t reg_device = BIT_DEV_MBS | BIT_DEV_LBA;
    if(hd->dev_no == 1) {//为从盘
        reg_device |= BIT_DEV_DEV;// 1 表示从盘
    }
    outb(reg_dev(hd->my_channel), reg_device);// 写到所在通道的 device 寄存器
}

// 向硬盘控制器写入起始扇区地址及要读写的扇区数
static void select_sector(struct disk* hd, uint32_t lba, uint8_t sec_cnt) {
    ASSERT(lba <= max_lba);// 判断位于 80M 内
    struct ide_channel* channel = hd->my_channel;

    // 先往 Sector count 寄存器写入待写入的扇区数
    outb(reg_sect_cnt(channel), sec_cnt);

    // 分别写入 lba （24-27位在 device 寄存器中
    outb(reg_lba_l(channel), lba);
    outb(reg_lba_m(channel), lba >> 8);
    outb(reg_lba_h(channel), lba >> 16);
    // 无法单独写入这24-27位到 device 中,所以在此处把device寄存器再重新写入一次
    outb(reg_dev(channel), BIT_DEV_MBS | BIT_DEV_LBA | (hd->dev_no == 1 ? BIT_DEV_DEV : 0) | lba >> 24);
}

// 向通道 channel 发命令 cmd 
static void cmd_out(struct ide_channel* channel, uint8_t cmd) {
    // 只要向硬盘发出了命令，便将 expect 置为 true，表明该通道正期待来自硬盘的中断
    channel->excepting_itr = true;
    outb(reg_cmd(channel), cmd);
}

// 硬盘读入sec_cnt个扇区的数据到buf 
static void read_from_sector(struct disk* hd, void* buf, uint8_t sec_cnt) {
    uint32_t size_in_byte;
    if (sec_cnt == 0) {
        // 若为 0 ，其实表示的是 256
        size_in_byte = 512 * 256;
    } else {
        size_in_byte = sec_cnt * 512;
    }
    // 从端口port读入的word_cnt个字写入addr,以 字 为单位
    insw(reg_data(hd->my_channel), buf, size_in_byte / 2);
}

// 将buf中sec_cnt扇区的数据写入硬盘 
static void write2sector(struct disk* hd, void* buf, uint8_t sec_cnt) {
    uint32_t size_in_byte;
    if (sec_cnt == 0) {
        // 若为 0 ，其实表示的是 256
        size_in_byte = 512 * 256;
    } else {
        size_in_byte = sec_cnt * 512;
    }
    outsw(reg_data(hd->my_channel), buf, size_in_byte / 2);    
}

// 由于硬盘是低速设备，因此在等待硬盘读写过程中，应当将 CPU 让出
// 等待30秒 ，其缘由在于 ATA 中规定所有操作需要在 30 秒之内完成
// 因此我们等待硬盘20秒，成功则返回 true 
static bool busy_wait(struct disk* hd) {
    struct ide_channel* channel = hd->my_channel;
    uint16_t time_limit = 30 * 1000;//毫秒为单位
    while(time_limit -= 10 >= 0) {
        // 判断 status 上的 bsy 位是否为 1，若为 1 则表示硬盘忙碌，则休眠 10 毫秒
        if(!(inb(reg_status(channel)) & BIT_STAT_BSY)) {
            return (inb(reg_status(channel)) & BIT_STAT_DRQ);// 判断是否已经准备好数据
        } else {
            mtime_sleep(10);// 让出 CPU 
        }
    }
}


// 从硬盘 hd 的扇区地址 lba 中 读取 sec_cnt 个扇区到 buf 
// 由于读写扇区数端口 0x1f2 和 0x172 为 8 位寄存器，因此每次最多读取 256 个扇区
// 当进行 > 256 个扇区读取时，需要进行拆分
void ide_read(struct disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt) { 
    ASSERT(lba <= max_lba);
    ASSERT(sec_cnt > 0);    
    // 先给 硬盘上锁，保证只操作同一通道上的一块硬盘
    lock_acquire(&hd->my_channel->lock);


    // 1、选择操作的硬盘
    select_disk(hd);

    uint32_t secs_op;    // 每次操作的扇区数
    uint32_t secs_done = 0;  // 已经完成的扇区数
    while(secs_done < sec_cnt) {//当还未完成
        if((secs_done + 256) <= sec_cnt) {
            secs_op = 256;
        } else {
            secs_op = sec_cnt - secs_done;
        }

    // 2、写入待读入的扇区数 和 起始扇区号
    select_sector(hd, lba + secs_done, secs_op);

    // 3、此时将执行命令 写入到 reg_cmd 寄存器中
    cmd_out(hd->my_channel, CMD_READ_SECTOR);// 准备开始读数据


   /*********************   阻塞自己的时机  ***********************
    在硬盘已经开始工作(开始在内部读数据或写数据)后才能阻塞自己,现在硬盘已经开始忙了,
    将自己阻塞,等待硬盘完成读操作后通过中断处理程序 sema_up 唤醒自己*/   
    // 此处采用 unblock 阻塞，而非 yield 阻塞  
    // 是由于 yield 唤醒后，还未达到满足条件时，还是会继续阻塞
    // 不如直接 unblock 待满足时才唤醒
    sema_down(&hd->my_channel->disk_done);


    // 4、检测硬盘状态是否可读
    // 醒来后开始执行以下代码
    if(!busy_wait(hd)) {//若失败
        char error[64];
        sprintf(error, "%s read sector %d failed!!!!!\n", hd->name, lba);
        PANIC(error);// 将其悬停，估计是硬件错误，很难解决
    }

    // 5、将数据从硬盘的缓冲区中读出
    read_from_sector(hd, (void*)((uint32_t)buf + secs_done * 512), secs_op);
    secs_done += secs_op;
    }
    // 至此 已经将所有读入到缓冲区中，打开锁即可
    lock_release(&hd->my_channel->lock);
}

// 将buf中sec_cnt扇区数据写入硬盘 
void ide_write(struct disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt) {
    ASSERT(lba <= max_lba);
    ASSERT(sec_cnt > 0);    
    // 先给 硬盘上锁，保证只操作同一通道上的一块硬盘
    lock_acquire(&hd->my_channel->lock);


    // 1、选择操作的硬盘
    select_disk(hd);

    uint32_t secs_op;    // 每次操作的扇区数
    uint32_t secs_done = 0;  // 已经完成的扇区数
    while(secs_done < sec_cnt) {//当还未完成
        if((secs_done + 256) <= sec_cnt) {
            secs_op = 256;
        } else {
            secs_op = sec_cnt - secs_done;
        }

    // 2、写入待写入的扇区数 和 起始扇区号
    select_sector(hd, lba + secs_done, secs_op);

    // 3、此时将执行命令 写入到 reg_cmd 寄存器中
    cmd_out(hd->my_channel, CMD_WRITE_SECTOR);// 准备开始写数据
    // 4、检测硬盘状态是否可写入
    // 醒来后开始执行以下代码
    if(!busy_wait(hd)) {//若失败
        char error[64];
        sprintf(error, "%s write sector %d failed!!!!!\n", hd->name, lba);
        PANIC(error);// 将其悬停，估计是硬件错误，很难解决
    }

    // 5、将数据写入硬盘
    write2sector(hd, (void*)((uint32_t)buf + secs_done * 512), secs_op);
    /*********************   阻塞自己的时机  ***********************
    在硬盘已经开始工作(开始在内部读数据或写数据)后才能阻塞自己,现在硬盘已经开始忙了,
    将自己阻塞,等待硬盘完成读操作后通过中断处理程序 sema_up 唤醒自己*/   
    // 此处采用 unblock 阻塞，而非 yield 阻塞  
    // 是由于 yield 唤醒后，还未达到满足条件时，还是会继续阻塞
    // 不如直接 unblock 待满足时才唤醒
    // 阻塞肯定是发生在硬盘开始操作之后。因此和读硬盘安排时机不同
    sema_down(&hd->my_channel->disk_done);

    secs_done += secs_op;
    }
    // 至此 已经将所有读入到缓冲区中，打开锁即可
    lock_release(&hd->my_channel->lock);
}

// 将 dst 中 len 个相邻字节交换位置后存入 buf 
static void swap_pairs_bytes(const char* dst, char* buf, uint32_t len) {
    uint8_t idx;
    for (idx = 0; idx < len; idx += 2) {
        // buf 中存储 dst 两相邻元素交换位置后的字符串
        buf[idx + 1] = *dst++;
        buf[idx] = *dst++;
    }
    buf[idx] = '\0';
}

// 获得硬盘参数信息 
static void identify_disk(struct disk* hd) {
    char id_info[512];//用于存储返回的硬盘参数
    // 1、选择硬盘
    select_disk(hd);
    // 2、写入读入命令
    cmd_out(hd->my_channel, CMD_IDENTIFY);

    // 向硬盘发生指令后，便阻塞自己
    // 待硬盘完成后，才通过中断程序唤醒自己
    sema_down(&hd->my_channel->disk_done);
    // 3、检测硬盘状态是否可写入
    // 醒来后开始执行以下代码
    if(!busy_wait(hd)) {//若失败
        char error[64];
        sprintf(error, "%s identify failed!!!!!\n", hd->name);
        PANIC(error);// 将其悬停，估计是硬件错误，很难解决
    }
    read_from_sector(hd, id_info, 1);// 读入信息

    // 至此 id_info 中已经是硬盘信息了
    // 由于硬盘参数信息是以 字 来存储
    // 因此需要通过 swap_pairs_bytes 用于存储转换
    char buf[64];
    uint8_t sn_start = 10 * 2, sn_len = 20, md_start = 27 * 2, md_len = 40;
    swap_pairs_bytes(&id_info[sn_start], buf, sn_len);
    // 输出序列号
    printk("   disk %s info:\n      SN: %s\n", hd->name, buf);
    memset(buf, 0, sizeof(buf));
    swap_pairs_bytes(&id_info[md_start], buf, md_len);
    // 输出型号
    printk("        MODULE: %s\n", buf);
    uint8_t sector_start = 60 * 2;// 刚好两个字，就不用交换了
    uint32_t sectors = *(uint32_t*)&id_info[sector_start];
    printk("        SECTORS: %d\n", sectors);
    printk("        CAPACITY: %dMB\n", sectors * 512 / 1024 /1024);
}

// 扫描硬盘 hd 中地址为 ext_lba 的扇区中的所有分区 
static void partition_scan(struct disk* hd, uint32_t ext_lba) {
    // 每个子扩展分区都有一个分区表， 因此 该函数 对每一个子扩展分区进行递归调用
    struct boot_sector* bs = sys_malloc(sizeof(struct boot_sector));
    ide_read(hd, ext_lba, bs, 1);//将扩展分区 1 扇区的数据读入到 bs 中，用于保存结构体 boot_sector
    uint8_t part_idx = 0;
    struct partition_table_entry* p = bs->partition_table;// 将分区表地址返回到 指针 p，之后对 p 进行遍历

    // 遍历分区表的 4 个表项
    while(part_idx++ < 4) {
        if(p->fs_type == 0x5) { // 若为扩展分区
            if(ext_lba_base != 0) {// 并非第一次读取，即不为主扩展分区  EBR
                // 子扩展分区的start_lba是相对于主引导扇区中的总扩展分区地址 
                partition_scan(hd, p->start_lba + ext_lba_base);
            } else { // 第一次读取引导块,也就是主引导记录所在的扇区 MBR
	        // 记录下扩展分区的起始lba地址, 后面所有的扩展分区地址都相对于此 
                ext_lba_base = p->start_lba;
                partition_scan(hd, p->start_lba);
            }
        } else if(p->fs_type != 0) {//表明此时不为扩展分区，但为其他有效分区类型
            if(ext_lba == 0) {// 说明此时为 MBR，由于扩展分区已经在上面进行处理了，
                            // 因此此时所需要处理的分区 ，必然为主分区
                hd->prim_parts[p_no].start_lba = ext_lba + p->start_lba;
                hd->prim_parts[p_no].sec_cnt = p->sec_cnt;
                hd->prim_parts[p_no].my_disk = hd;
                // 将分区加入到分区列表中
                list_append(&partition_list, &hd->prim_parts[p_no].part_tag);
                // 名字形式为 [x]d[y][n]  sda1
                sprintf(hd->prim_parts[p_no].name, "%s%d", hd->name, p_no + 1);
                p_no++;
                ASSERT(p_no < 4);   //只能有 4 个
            } else { // 有效逻辑分区
                hd->logic_parts[l_no].start_lba = ext_lba + p->start_lba;
                hd->logic_parts[l_no].sec_cnt = p->sec_cnt;
                hd->logic_parts[l_no].my_disk = hd;
                list_append(&partition_list, &hd->logic_parts[l_no].part_tag);
                sprintf(hd->logic_parts[l_no].name, "%s%d", hd->name, l_no + 5); // 逻辑分区数字是从5开始,主分区是1～4.
                l_no++;
                if (l_no >= 8) // 只支持8个逻辑分区,避免数组越界
                    return;
            }
        }
        p++;
    }
    sys_free(bs);
}

// 打印分区信息
// #define UNUSED __attribute__ ((unused)) 
// 由于用于 list_traverse 函数中必须要两个参数
// 
static bool partition_info(struct list_elem* pelem, int arg UNUSED) {
    struct partition* part = elem2entry(struct partition, part_tag, pelem);
    printk("    %s start_lba:0x%x, sec_cnt:0x%x\n", part->name, part->start_lba, part->sec_cnt);


    /* 在此处return false与函数本身功能无关,
    * 只是为了让主调函数list_traversal继续向下遍历元素 */
    return false;
}

//  硬盘中断处理程序 
void intr_hd_handler(uint8_t irq_no) {
    ASSERT(irq_no == 0x2e || irq_no == 0x2f);
    uint8_t ch_no = irq_no - 0x2e;
    struct ide_channel* channel = &channels[ch_no];//获取端口号基准值
    ASSERT(channel->irq_no == irq_no);

    // 通过 cmd_out 发号施令时，将 except 置为 true，表示期待中断的到来
    // 至于其余的 硬盘本身问题，不予考虑
    if(channel->excepting_itr) {
        channel->excepting_itr = false;
        sema_up(&channel->disk_done);
    }

    // 由于硬盘在没有得到中断已经完成的通知下，是不会继续进行中断的
    // 因此 需要显示的告诉硬盘中断已经完成
    // 此处采用 读取状态寄存器 使硬盘控制器认为此次的中断已被处理,
    // 从而硬盘可以继续执行新的读写
    // 这是由于硬盘控制器的中断会在读取 status 寄存器后被清理掉
    inb(reg_status(channel));
}

// 硬盘数据结构初始化
void ide_init(void) {
    printk("ide_init start\n");
    uint8_t hd_cnt = *((uint8_t*)(0x475));   // 获取硬盘数量
    ASSERT(hd_cnt > 0);

    list_init(&partition_list);
    channel_cnt = DIV_ROUND_UP(hd_cnt, 2);// 一个 ide 通道上有两个硬盘

    struct ide_channel* channel;
    // 在地址 0x475 可以获取硬盘数量
    uint8_t channel_no = 0;
    uint8_t dev_no = 0;//用于指示打印第几个硬盘信息

    // 处理每个通道上的硬盘
    while (channel_no < channel_cnt) {
        channel = &channels[channel_no];
        // printf("aaaa\n"); // test
        sprintf(channel->name, "ide%d", channel_no);

        // 为每个ide通道初始化端口基址及中断向量
        switch (channel_no) {
        case 0:
            channel->port_base = 0x1f0; // ide0 的起始端口号为 0x1f0
            // ide0通道的的中断向量号 0x2e
            channel->irq_no = 0x20 + 14;    // 从片 8259A 的
            break;
        case 1:
	        channel->port_base	 = 0x170;	   // ide1通道的起始端口号是0x170
	        // ide1通道的的中断向量号 0x2f
            channel->irq_no	 = 0x20 + 15;	   // 从8259A上的最后一个中断引脚,我们用来响应ide1通道上的硬盘中断
            break;
        default:
            break;
        }

        // 初始化信号量，锁，excepting 为 false
        channel->excepting_itr = false;
        lock_init(&channel->lock);
        /* 初始化为0,目的是向硬盘控制器请求数据后,硬盘驱动sema_down此信号量会阻塞线程,
        直到硬盘完成后通过发中断,由中断处理程序将此信号量sema_up,唤醒线程. */        
        sema_init(&channel->disk_done, 0);

        register_handler(channel->irq_no, intr_hd_handler);

        // channel_no++;

        // 分别获取两个硬盘的参数及分区信息 
        while (dev_no < 2) {
            struct disk* hd = &channel->devices[dev_no];
            hd->my_channel = channel;
            hd->dev_no = dev_no;
            sprintf(hd->name, "sd%c", 'a' + channel_no * 2 + dev_no);
            identify_disk(hd);
            if (dev_no != 0) {// 内核所在的 裸硬盘 hd60M.img 不处理
                partition_scan(hd, 0);// 扫描 hd80M.img 的分区
            }
            p_no = 0, l_no = 0;
            dev_no++;
        }
        dev_no = 0;// 将硬盘驱动器置为 0，为下一个 channel 的两个硬盘初始化
        channel_no++;
    }
    printk("\n    all partition info:\n");
    // 打印所有分区信息
    list_traversal(&partition_list, partition_info, (int)NULL);
    printk("ide_init done!\n");
}

