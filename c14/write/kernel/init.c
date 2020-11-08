#include "init.h"
#include "print.h"
#include "interrupt.h"
#include "timer.h"
#include "memory.h"
#include "thread.h"
#include "console.h"
#include "keyboard.h"
#include "tss.h"
#include "syscall-init.h"
#include "ide.h"
#include "fs.h"

/*负责初始化所有模块 */
void init_all() {
    put_str("init_all start!\n");
    idt_init();// 初始化中断
    
    mem_init();//初始化内存管理系统
    thread_environment_init();// 初始化线程相关环境
    timer_init();   // 初始化 PIT
    console_init(); // 初始化 console 
    keyboard_init();
    tss_init();
    syscall_init();   // 初始化系统调用
    intr_enable();      // 后面的 ide_int 需要打开中断
    ide_init();     //初始化硬盘
    filesys_init(); // 初始化文件系统
}