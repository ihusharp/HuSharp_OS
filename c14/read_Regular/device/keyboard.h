#ifndef __DEVICE_KEYBOARD_H
#define __DEVICE_KEYBOARD_H
void keyboard_init(void); 
// 由于要实现生产者消费者，因此需要满足 键盘缓冲区为全局变量
extern struct ioqueue kbd_buf;
#endif
