#include "syscall-init.h"
#include "syscall.h"
#include "stdint.h"
#include "print.h"
#include "thread.h"
#include "console.h"
#include "string.h"
#include "memory.h"
#include "fs.h"
#include "fork.h"
#include "exec.h"

// 定义 调用子功能数组
#define syscall_nr 32
typedef void* syscall;
syscall syscall_table[syscall_nr];

/*********   内核空间中的函数 + sys   ***************/
// 返回当前任务的 pid 
uint32_t sys_getpid(void) {
   return running_thread()->pid;
}

// 现在的 sys_write 中 fs.h 中实现
// // 打印字符串（未实现文件系统前版本）
// // 返回字符串长度
// uint32_t sys_write(char* str) {
//    console_put_str(str);
//    return strlen(str);
// }

/********************************************/

// 初始化系统调用
void syscall_init(void) {
   put_str("syscall_init start!\n");
   syscall_table[SYS_GETPID] = sys_getpid;
   syscall_table[SYS_WRITE] = sys_write;
   syscall_table[SYS_MALLOC] = sys_malloc;
   syscall_table[SYS_FREE] = sys_free;
   syscall_table[SYS_FORK] = sys_fork;
   syscall_table[SYS_READ] = sys_read;
   syscall_table[SYS_PUTCHAR] = sys_putchar;
   syscall_table[SYS_CLEAR]   = cls_screen;// 位于 print.S
   syscall_table[SYS_GETCWD]     = sys_getcwd;
   syscall_table[SYS_OPEN]       = sys_open;
   syscall_table[SYS_CLOSE]      = sys_close;
   syscall_table[SYS_LSEEK]	 = sys_lseek;
   syscall_table[SYS_UNLINK]	 = sys_unlink;
   syscall_table[SYS_MKDIR]	 = sys_mkdir;
   syscall_table[SYS_OPENDIR]	 = sys_opendir;
   syscall_table[SYS_CLOSEDIR]   = sys_closedir;
   syscall_table[SYS_CHDIR]	 = sys_chdir;
   syscall_table[SYS_RMDIR]	 = sys_rmdir;
   syscall_table[SYS_READDIR]	 = sys_readdir;
   syscall_table[SYS_REWINDDIR]	 = sys_rewinddir;
   syscall_table[SYS_STAT]	 = sys_stat;
   syscall_table[SYS_PS]	 = sys_ps;
   syscall_table[SYS_EXECV]	 = sys_execv;
   put_str("syscall_init done!\n");
}