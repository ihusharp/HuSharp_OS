#include "time.h"
#include "io.h"
#include "kernel/print.h"


#define IRQ0_FREQUENCY	   100  // 时钟频率
#define INPUT_FREQUENCY	   1193180  // 计数器原本的工作频率
#define COUNTER0_VALUE	   INPUT_FREQUENCY / IRQ0_FREQUENCY //计数器 0 初始值 
#define COUNTRER0_PORT	   0x40     // 计数器 0 的接口
#define COUNTER0_NO	   0        // 计数器号码
#define COUNTER_MODE	   2    // 模式 2
#define READ_WRITE_LATCH   3    // 读写方式，先读写低 8 位，再读写高 8 位
#define PIT_CONTROL_PORT   0x43 // 控制器寄存器接口

/* 
 * 把操作的计数器counter_no、读写锁属性rwl、计数器模式counter_mode
 * 写入模式控制寄存器
 * 并赋予初始值counter_value
 *  */
static void frequency_set(uint8_t counter_port, \
                uint8_t counter_no, \
                uint8_t rwl, \
                uint8_t counter_mode, \
                uint16_t counter_value) {
/* 往控制字寄存器端口0x43中写入控制字 */
// 最后一位为 0 ，即选择二进制
    outb(PIT_CONTROL_PORT, (uint8_t)(counter_no << 6 | rwl << 4 | counter_mode << 1));
/* 先写入counter_value的低8位 */
    outb(counter_port, (uint8_t)counter_value);
/* 后写入counter_value的高8位 */    
    outb(counter_port, (uint8_t)counter_port >> 8);
}

// 初始化 8253
void timer_init() {
    put_str("timer_init start!\n");
    frequency_set(COUNTRER0_PORT, COUNTER0_NO, READ_WRITE_LATCH, COUNTER_MODE, COUNTER0_VALUE);
    put_str("timer_init done!\n");
}
