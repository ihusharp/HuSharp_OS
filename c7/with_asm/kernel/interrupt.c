# include "stdint.h"
# include "global.h"
# include "io.h"
# include "interrupt.h"

#define IDT_DESV_CNT 0x21   // 现在支持的中断数 33

// 这里用的可编程中断控制器是8259A
#define PIC_M_CTRL 0x20	       // 主片的控制端口是0x20
#define PIC_M_DATA 0x21	       // 主片的数据端口是0x21
#define PIC_S_CTRL 0xa0	       // 从片的控制端口是0xa0
#define PIC_S_DATA 0xa1	       // 从片的数据端口是0xa1

// 中断门描述符结构体
struct gate_desc{
    uint16_t func_offset_low_word;
    uint16_t selector;
    uint8_t dcount;     // 表示双字计数段门描述符的第 4 字节，为固定值

    uint8_t attribute;
    uint16_t func_offset_high_word;  
};

// 创建中断门描述符函数，参数：指针、属性、描述符对应的中断函数
static void make_idt_desc(struct gate_desc* p_gdesc, uint8_t attr, intr_handler function);
static struct gate_desc idt[IDT_DESV_CNT];  // 中断描述符数组

extern intr_handler intr_entry_table[IDT_DESV_CNT];

/* 初始化可编程中断控制器8259A */
static void pic_init(void) {
    // 初始化主片
    outb(PIC_M_CTRL, 0x11); //    ICW1：边沿触发， 级联8259， 需要 ICW4
    outb(PIC_M_DATA, 0x20); //    ICW2: 起始中断向量号为 0x20, 也就是IR[0-7] 为 0x20 ~ 0x27.
    outb(PIC_M_DATA, 0x04); //    ICW3: IR2 接从片
    outb(PIC_M_DATA, 0x01); //    ICW4: 8086模式， 正常EOI

    /* 初始化从片 */
    outb(PIC_S_CTRL, 0x11); //    ICW1: 边沿触发,级联8259, 需要ICW4.
    outb(PIC_S_DATA, 0x28); //    ICW2: 起始中断向量号为0x28,也就是IR[8-15] 为 0x28 ~ 0x2F.
    outb(PIC_S_DATA, 0x02); //    ICW3: 设置从片连接到主片的IR2引脚
    outb(PIC_S_DATA, 0x01); //    ICW4: 8086 模式，正常 EOI

    // 打开主片上 IR0 ，也就是目前只接受时钟中断
    outb(PIC_M_DATA, 0xfe);
    outb(PIC_S_DATA, 0xff);

    put_str("   pic_init done!\n");
}


/**
 * 创建中断门描述符.
 */ 
static void make_idt_desc(struct gate_desc* p_gdesc, uint8_t attr, intr_handler function) {
    p_gdesc->func_offset_low_word = (uint32_t) function & 0x0000FFFF;
    p_gdesc->selector = SELECTOR_K_CODE;
    p_gdesc->dcount = 0;
    p_gdesc->attribute = attr;
    p_gdesc->func_offset_high_word = ((uint32_t) function & 0xFFFF0000) >> 16;
}

/**
 * 初始化中断描述符表.
 */ 
static void idt_desc_init(void) {
    int i;
    for (i = 0; i < IDT_DESV_CNT; i++){
        make_idt_desc(&idt[i], IDT_DESC_ATTR_DPL0, intr_entry_table[i]);
    }
    put_str("   idt_desc_init done!\n");
}

/*完成有关中断的所有初始化工作*/
void idt_init() {
    put_str("idt_init start!\n");
    idt_desc_init();    // 初始化中断描述符表
    pic_init();     // 初始化 8259A

    // 加载 idt
    // 尽管 LDTR 为 48位，我们没有 48 位变量，但是，可以采用 64 位，提取前 48 位即可
    // 指针只能转换为 对应长度 整型，因此需要先转换为 32 位
    uint64_t idt_operand = ((sizeof(idt) - 1) | ((uint64_t)(uint32_t)idt << 16));
    asm volatile("lidt %0" : : "m"(idt_operand));// m 表示 内存约束，实则是将idt_operand 地址传给 lidt
    put_str("idt_init done!\n");
}