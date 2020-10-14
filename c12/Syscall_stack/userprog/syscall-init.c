#include "syscall-init.h"
#include "syscall.h"
#include "stdint.h"
#include "print.h"
#include "thread.h"
// 内核空间中的函数 + sys

// 定义 调用子功能数组
#define syscall_nr 32

typedef void* syscall;

syscall syscall_table[syscall_nr];

// 初始化系统调用
void syscall_init(void) {
   put_str("syscall_init start!\n");
   syscall_table[SYS_GETPID] = sys_getpid;
   put_str("syscall_init done!\n");
}

// 返回当前任务的 pid 
uint32_t sys_getpid(void) {
   return running_thread()->pid;
}



