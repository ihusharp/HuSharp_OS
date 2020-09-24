#include "print.h"
#include "init.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"

void k_thread_HuSharp_1(void* args);

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
    thread_start("k_thread_HuSharp_1", 31, k_thread_HuSharp_1, "zdy is a shazi! ");

    while(1);
    return 0;
}

void k_thread_HuSharp_1(void* args) {
    char* para = args;
    while(1) {
        put_str(para);
    }
}