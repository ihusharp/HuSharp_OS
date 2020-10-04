#include "kernel/print.h"
#include "init.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"

// 临时为测试添加
#include "ioqueue.h"
#include "keyboard.h"

void k_thread_HuSharp_1(void* args);
void k_thread_HuSharp_2(void* args);
void k_thread_HuSharp_3(void* args);
int main(void) {
    put_str("I am kernel!\n");
    init_all();
    //ASSERT(1 == 2);
    // asm volatile("sti");    // 打开中断 即将 EFLAGS 的 IF置为 1

    // 进行内存分配
    /* 内核物理页分配 
    void* addr = get_kernel_pages(3);
    put_str("\n get_kernel_page start vaddr is: ");
    put_int((uint32_t) addr);
    put_str("\n"); 
    */

    // 线程演示
    thread_start("k_thread_HuSharp_1", 31, k_thread_HuSharp_1, "agrA 31 ");
    thread_start("k_thread_HuSharp_2", 8, k_thread_HuSharp_2, "agrB 8 ");
    // thread_start("k_thread_HuSharp_3", 20, k_thread_HuSharp_3, "agrC 20 ");

    intr_enable();// 打开时钟中断

    while(1) {
        // console_put_str("Main ");
    }
    return 0;
}

void k_thread_HuSharp_1(void* args) {
    char* para = args;
    while(1) {
        enum intr_status old_status = intr_disable();
        if(ioq_empty(&kbd_buf)) {
            console_put_str(args);
            char byte = ioq_getchar(&kbd_buf);
            console_put_char(byte);
        }
        intr_set_status(old_status);
    }
}

void k_thread_HuSharp_2(void* args) {
    char* para = args;
    while(1) {
        enum intr_status old_status = intr_disable();
        if(ioq_empty(&kbd_buf)) {
            console_put_str(args);
            char byte = ioq_getchar(&kbd_buf);
            console_put_char(byte);
        }
        intr_set_status(old_status);
    }
}

void k_thread_HuSharp_3(void* args) {
    char* para = args;
    while(1) {
        console_put_str(para);
    }
}