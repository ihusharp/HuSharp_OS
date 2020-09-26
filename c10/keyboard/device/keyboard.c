#include "keyboard.h"
#include "print.h"
#include "interrupt.h"
#include "io.h"
#include "global.h"

#define KBD_BUF_PORT 0x60   // 键盘 buffer 寄存器端口号为 0x60

// 键盘中断程序
static void intr_keyboard_handler(void) {
    // 输出 a
    put_char('a');
    // 必须读取输出缓冲区寄存器，否则 8042 不再响应键盘中断
    inb(KBD_BUF_PORT);
    // uint8_t scancode = inb(KBD_BUF_PORT);
    // put_int(scancode);
    return;
}

// 键盘初始化
void keyboard_init() {
    put_str("keyboard init start\n");
    register_handler(0x21, intr_keyboard_handler);
    put_str("keyboard init done\n");
}