#ifndef __LIB_USER_SYSCALL_H
#define __LIB_USER_SYSCALL_H
#include "stdint.h"

// 表示系统调用功能号
enum SYSCALL_NR {
    SYS_GETPID
};
uint32_t getpid(void);

#endif