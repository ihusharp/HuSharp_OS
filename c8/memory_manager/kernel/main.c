#include "kernel/print.h"
#include "init.h"
#include "debug.h"
#include "memory.h"

int main(void) {
    put_str("I am kernel!\n");
    init_all();
    //ASSERT(1 == 2);
    // asm volatile("sti");    // 打开中断 即将 EFLAGS 的 IF置为 1

    // 进行内存分配
    void* addr = get_kernel_pages(3);
    put_str("\n get_kernel_page start vaddr is: ");
    put_int((uint32_t) addr);
    put_str("\n");

    while(1);
    return 0;
}