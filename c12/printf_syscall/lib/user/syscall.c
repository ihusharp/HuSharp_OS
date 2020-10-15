#include "syscall.h"

/* 无参数的系统调用 */
#define _syscall0(NUMBER) ({                         \
    int retval;                                      \
    asm volatile(                                    \
        "pushl %[number]; int $0x80; addl $4, %%esp" \
        : "=a"(retval)                               \
        : [ number ] "i"(NUMBER)                     \
        : "memory");                                 \
    retval;                                          \
})

/* 一个参数的系统调用 */
#define _syscall1(NUMBER, ARG0) ({                                  \
    int retval;                                                     \
    asm volatile(                                                   \
        "pushl %[arg0]; pushl %[number]; int $0x80; addl $8, %%esp" \
        : "=a"(retval)                                              \
        : [ number ] "i"(NUMBER), [ arg0 ] "g"(ARG0)                \
        : "memory");                                                \
    retval;                                                         \
})

/* 两个参数的系统调用 */
#define _syscall2(NUMBER, ARG0, ARG1) ({              \
    int retval;                                       \
    asm volatile(                                     \
        "pushl %[arg2]; pushl %[arg1]; "              \
        "pushl %[number]; int $0x80; addl $16, %%esp" \
        : "=a"(retval)                                \
        : [ number ] "i"(NUMBER),                     \
        [ arg0 ] "g"(ARG0),                           \
        [ arg1 ] "g"(ARG1)                            \
        : "memory");                                  \
    retval;                                           \
})

// 三个参数的系统调用
// 按照 c 调用约定，将三个参数从右往左依次入栈
// pushl %[number]; int $0x80; addl $16, %%esp:
// 做了三件事：压入功能号， 触发中断， 栈指针跨越四个参数
#define _syscall3(NUMBER, ARG0, ARG1, ARG2) ({          \
    int retval;                                         \
    asm volatile(                                       \
        "pushl %[arg2]; pushl %[arg1]; pushl %[arg0]; " \
        "pushl %[number]; int $0x80; addl $16, %%esp"   \
        : "=a"(retval)                                  \
        : [ number ] "i"(NUMBER),                       \
        [ arg0 ] "g"(ARG0),                             \
        [ arg1 ] "g"(ARG1),                             \
        [ arg2 ] "g"(ARG2)                              \
        : "memory");                                    \
    retval;                                             \
})

// 为 syscall_init 中的用户接口
// 返回当前任务 pid
uint32_t getpid()
{
    return _syscall0(SYS_GETPID);
}

// 打印字符串
uint32_t write(char* str) {
    return _syscall1(SYS_WRITE, str);
}