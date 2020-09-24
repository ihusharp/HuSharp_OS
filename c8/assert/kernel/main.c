#include "kernel/print.h"
#include "init.h"
#include "debug.h"

int main(void) {
    put_str("I am kernel!\n");
    init_all();
    ASSERT(1 == 2);
    // asm volatile("sti");    // 打开中断 即将 EFLAGS 的 IF置为 1
    while(1);
    return 0;
}