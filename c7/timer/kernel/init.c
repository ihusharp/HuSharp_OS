#include "init.h"
#include "kernel/print.h"
#include "interrupt.h"
#include "../device/timer.h"

/*负责初始化所有模块 */
void init_all() {
    put_str("init_all start!\n");
    idt_init();
    timer_init();
}