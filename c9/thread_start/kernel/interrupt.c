#include "stdint.h"
#include "global.h"
#include "io.h"
#include "print.h"
#include "interrupt.h"

#define IDT_DESC_CNT 0x21   // 现在支持的中断数 33

// 这里用的可编程中断控制器是8259A
#define PIC_M_CTRL 0x20	       // 主片的控制端口是0x20
#define PIC_M_DATA 0x21	       // 主片的数据端口是0x21
#define PIC_S_CTRL 0xa0	       // 从片的控制端口是0xa0
#define PIC_S_DATA 0xa1	       // 从片的数据端口是0xa1

#define EFLAGS_IF   0x00000200  // eflags 寄存器的 if 位为 1
#define GET_EFLAGS(EFLAG_VAR) asm volatile("pushfl; popl %0" : "=g" (EFLAG_VAR))

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
static struct gate_desc idt[IDT_DESC_CNT];  // 中断描述符数组

/**
 * 中断的名称.
 */ 
char* intr_name[IDT_DESC_CNT];      // 用于保存异常名字的数组
// 中断处理程序数组.在kernel.S中定义的intrXXentry只是中断处理程序的入口,
// 最终调用的是ide_table中的处理程序
intr_handler idt_table[IDT_DESC_CNT];   
// 声明引用定义在kernel.S中的中断处理函数入口数组
extern intr_handler intr_entry_table[IDT_DESC_CNT]; 

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
    for (i = 0; i < IDT_DESC_CNT; i++){
        make_idt_desc(&idt[i], IDT_DESC_ATTR_DPL0, intr_entry_table[i]);
    }
    put_str("   idt_desc_init done!\n");
}

/* 通用的中断处理函数,一般用在异常出现时的处理 */
static void general_intr_handler(uint8_t vec_nr) {
    // 处理伪中断，，无法通过 IMR 寄存器屏蔽，因此单独处理
    if(vec_nr == 0x27 || vec_nr == 0x2f) {
        return;
    }
    // 将光标置为 0 ，从屏幕左上角清出一片打印异常信息的区域
    set_cursor(0);// 有时候甚至可能中断错误是由光标错误引起
    // 因此先将左上角空出四行来
    int cursor_pos = 0;
    while(cursor_pos < 320) {
        put_char(' ');
        cursor_pos++;
    }
    // 至此 已经将左上角空出四行，因此将光标再移到 0 位置，进行输出
    set_cursor(0);
    put_str("!!!!!!     exception message begin     !!!!!!");
    set_cursor(88);// 第二行第 8 列
    put_str(intr_name[vec_nr]);
    if(vec_nr == 14) {  // 若为 pagefault，将缺失的地址打印出来
        int page_fault_vaddr = 0;
        // 发生 pagefult
        asm("movl %%cr2, %0" : "=r" (page_fault_vaddr));// cr2 是存放造成 page_fault 的地址
        put_str("\npage fault addr is ");
        put_int(page_fault_vaddr);
    }
    put_str("!!!!!!     exception message end     !!!!!!");
    // 能进入中断处理程序就表示已经处在关中断情况下
    // 不会出现
}

/* 完成一般中断处理函数注册及异常名称注册 */
static void 
exception_init(void) {			    // 完成一般中断处理函数注册及异常名称注册
    int i;
    for (i = 0; i < IDT_DESC_CNT; i++){
        // 默认为general_intr_handler。
        // 以后会由register_handler来注册具体处理函数。
        idt_table[i] =  general_intr_handler;
        intr_name[i] = "unknown"; // 先统一赋值为unknown 
    }
    intr_name[0] = "#DE Divide Error";
    intr_name[1] = "#DB Debug Exception";
    intr_name[2] = "NMI Interrupt";
    intr_name[3] = "#BP Breakpoint Exception";
    intr_name[4] = "#OF Overflow Exception";
    intr_name[5] = "#BR BOUND Range Exceeded Exception";
    intr_name[6] = "#UD Invalid Opcode Exception";
    intr_name[7] = "#NM Device Not Available Exception";
    intr_name[8] = "#DF Double Fault Exception";
    intr_name[9] = "Coprocessor Segment Overrun";
    intr_name[10] = "#TS Invalid TSS Exception";
    intr_name[11] = "#NP Segment Not Present";
    intr_name[12] = "#SS Stack Fault Exception";
    intr_name[13] = "#GP General Protection Exception";
    intr_name[14] = "#PF Page-Fault Exception";
    // intr_name[15] 第15项是intel保留项，未使用
    intr_name[16] = "#MF x87 FPU Floating-Point Error";
    intr_name[17] = "#AC Alignment Check Exception";
    intr_name[18] = "#MC Machine-Check Exception";
    intr_name[19] = "#XF SIMD Floating-Point Exception";
}

/* 开中断并返回开中断前的状态*/
enum intr_status intr_enable() {
    enum intr_status old_status;
    if(INTR_ON == intr_get_status()) {
        old_status = INTR_ON;
        return old_status;
    } else {
        old_status = INTR_OFF;
        asm volatile("sti");    // 开中断， sti 指令将 IF 位 置 1
        return old_status;
    }
}

/* 关中断,并且返回关中断前的状态 */
enum intr_status intr_disable() {
    enum intr_status old_status;
    if(INTR_ON == intr_get_status()) {
        old_status = INTR_ON;
        asm volatile("cli" : : : "memory"); // 关中断，cli 指令将 IF 位 置 0 
        return old_status;        
    } else {
        old_status = INTR_OFF;
        return old_status;
    }
}


/* 将中断状态设置为status */
enum intr_status intr_set_status(enum intr_status status) {
    return status & INTR_ON ? intr_enable() : intr_disable();
}

/* 获取当前中断状态 */
enum intr_status intr_get_status() {
    uint32_t eflags = 0;
    GET_EFLAGS(eflags);
    // 判断 IF 位的值是否为 1
    return (EFLAGS_IF & eflags) ? INTR_ON : INTR_OFF;
}

/*完成有关中断的所有初始化工作*/
void idt_init() {
    put_str("idt_init start!\n");
    idt_desc_init();    // 初始化中断描述符表
    exception_init();
    pic_init();     // 初始化 8259A

    // 加载 idt
    // 尽管 LDTR 为 48位，我们没有 48 位变量，但是，可以采用 64 位，提取前 48 位即可
    // 指针只能转换为 对应长度 整型，因此需要先转换为 32 位
    uint64_t idt_operand = ((sizeof(idt) - 1) | ((uint64_t)(uint32_t)idt << 16));
    asm volatile("lidt %0" : : "m"(idt_operand));// m 表示 内存约束，实则是将idt_operand 地址传给 lidt
    put_str("idt_init done!\n");
}