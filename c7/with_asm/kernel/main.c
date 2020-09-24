#include "kernel/print.h"
#include "init.h"

void main(void) {
    put_str("I am kernel!\n");
    init_all();
    asm volatile("sti");    // 打开中断 即将 EFLAGS 的 IF置为 1
    while(1);
}