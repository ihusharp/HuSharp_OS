#include "time.h"
#include "io.h"
#include "print.h"
#include "interrupt.h"
#include "debug.h"
#include "thread.h"

#define IRQ0_FREQUENCY	   100  // 时钟频率
#define INPUT_FREQUENCY	   1193180  // 计数器原本的工作频率
#define COUNTER0_VALUE	   INPUT_FREQUENCY / IRQ0_FREQUENCY //计数器 0 初始值 
#define COUNTRER0_PORT	   0x40     // 计数器 0 的接口
#define COUNTER0_NO	   0        // 计数器号码
#define COUNTER_MODE	   2    // 模式 2
#define READ_WRITE_LATCH   3    // 读写方式，先读写低 8 位，再读写高 8 位
#define PIT_CONTROL_PORT   0x43 // 控制器寄存器接口

// 每多少毫秒发生一次中断（时钟频率为 每秒 IRQ0_FREQUENCY 次）
#define mil_seconds_per_intr (1000 / IRQ0_FREQUENCY)


uint32_t ticks;     // ticks 是内核自中断开启以来总共的滴答数

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

// 时钟的中断处理函数 
static void intr_timer_handler(void) {
    struct task_struct* cur_thread = running_thread();// 获取当前线程

    ASSERT(cur_thread->stack_magic == 0x20000611);// 检查栈是否溢出

    cur_thread->elapsed_ticks++;    // 
    //从内核第一次处理时间中断后开始至今的滴哒数,内核态和用户态总共的嘀哒数
    ticks++;

    if(cur_thread->ticks == 0) {// 时间片用完，那就调度新的线程
        schedule(); 
    }else { // 否则将时间片 -1
        cur_thread->ticks--;
    }

}

// 让任务休眠 sleep_ticks 个 ticks（即通过下面的 mtime_sleep 函数
// 先将 毫秒数改成 ticks 为单位，然后再进行休眠
static void ticks_to_sleep(uint32_t sleep_ticks) {
    uint32_t start_ticks = ticks;// 获取此时的 ticks

    // 时刻判断此时的 ticks ，若还没达到足够的时钟中断（ticks由 intr_timer_handler 来更新）
    while (ticks - start_ticks < sleep_ticks) {
        // yeild 的作用是将 cpu 让出，与 block 不同在于，让出后还是在就绪队列中
        thread_yield();
    }
}

// 简易休眠函数 以毫秒为单位的sleep
void mtime_sleep(uint32_t m_seconds) {
    // 让任务休眠 sleep_ticks 个
    uint32_t sleep_ticks = DIV_ROUND_UP(m_seconds, mil_seconds_per_intr);
    ASSERT(sleep_ticks > 0);
    // 将毫秒数改成 ticks 为单位，然后再进行休眠
    ticks_to_sleep(sleep_ticks);
}

// 初始化 8253
void timer_init(void) {
    put_str("timer_init start!\n");
    frequency_set(COUNTRER0_PORT, COUNTER0_NO, READ_WRITE_LATCH, COUNTER_MODE, COUNTER0_VALUE);
    register_handler(0x20, intr_timer_handler);// 将时钟中断置为 0x20
    put_str("timer_init done!\n");
}


