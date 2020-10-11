#include "tss.h"
#include "stdint.h"
#include "global.h"
#include "string.h"
#include "print.h"
#include "thread.h"

#define PG_SIZE 4096

// 任务状态段 tss 结构
// tss 由程序员提供 CPU 来维护
struct tss
{
    uint32_t backlink;
    uint32_t* esp0;
    uint32_t ss0;
    uint32_t* esp1;
    uint32_t ss1;
    uint32_t* esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t (*eip) (void);
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint32_t trace;
    uint32_t io_base;
};
static struct tss tss;

// 更新 tss 中 esp0 字段的值 为 pthread 的 0 级栈
void update_tss_esp(struct task_struct* pthread) {
    tss.esp0 = (uint32_t*) ((uint32_t)pthread + PG_SIZE);
}

// 通过 c 来创建 gdt 描述符
// 此函数并非在 GDT 中生成，而是返回一个填充好的 描述符，还未安上GDT
static struct gdt_desc make_gdt_desc(uint32_t* desc_addr, uint32_t limit, uint8_t attr_low, uint8_t attr_high) {
    uint32_t desc_base = (uint32_t)desc_addr;
    struct gdt_desc desc;// 填充描述符
    desc.limit_low_word = limit & 0x0000ffff;
    desc.base_low_word = desc_base & 0x0000ffff;
    desc.base_mid_byte = ((desc_base & 0x00ff0000) >> 16);
    desc.attr_low_byte = (uint8_t)(attr_low);
    desc.limit_high_attr_high = (((limit & 0x000f0000) >> 16) + (uint8_t)(attr_high));
    desc.base_high_byte = desc_base >> 24;
    return desc;
}

// 在gdt中创建tss并重新加载gdt 
// 还安装两个用户进程使用的描述符到gdt中
void tss_init(void) {
    put_str("tss_init start!\n");
    uint32_t tss_size = sizeof(tss);
    memset(&tss, 0, tss_size);// 初始化
    tss.ss0 = SELECTOR_K_STACK;// 选择子
    tss.io_base = tss_size;//表示没有位图

    // 8 * 4 = 32 = 0x20
    // gdt 中描述符第 0 个不可用，第 1 个为代码段，第 2 个为数据段和栈，第 3 个为显存段
    // 因此将 tss 放在第 4 个  即0xc0000900 + 0x20
    *((struct gdt_desc*)0xc0000920) = make_gdt_desc((uint32_t*)&tss, tss_size - 1, TSS_ATTR_LOW, TSS_ATTR_HIGH);

    // 此时在gdt中添加dpl为3的数据段和代码段描述符
    // 作为用户进程的提前准备
    // 因此在 gdt 中的位置便依次顺延到 0x28 0x30
    *((struct gdt_desc*)0xc0000928) = make_gdt_desc((uint32_t*)0, 0xfffff, GDT_CODE_ATTR_LOW_DPL3, GDT_ATTR_HIGH);
    *((struct gdt_desc*)0xc0000930) = make_gdt_desc((uint32_t*)0, 0xfffff, GDT_DATA_ATTR_LOW_DPL3, GDT_ATTR_HIGH);

    // 由于更改了 gdt ，因此需要重新加载 lgdt
    // lgdt 的指令格式为：16位表界限 和 32位表的起始地址
    // 现在gdt中共有 7 个描述符，因此 表界限值为：7*8 - 1 
    // 且由于操作数中的 高 32 位为 GDT起始地址
    // 因此先将 0xc0000900 转换为32位，再转换为 64位（不可直接转换）再进行按位或
    uint64_t gdt_operand = ((8 * 7 - 1) | ((uint64_t)(uint32_t)0xc0000900 << 16));

    asm volatile("lgdt %0" : : "m" (gdt_operand));
    asm volatile("ltr %w0" : : "r" (SELECTOR_TSS));
    put_str("tss_init and ltr done!\n");

}