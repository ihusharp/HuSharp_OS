#include "shell.h"
#include "stdint.h"
#include "fs.h"
#include "file.h"
#include "syscall.h"
#include "stdio.h"
#include "global.h"
#include "assert.h"
#include "string.h"

#define cmd_len 128     // 最大支持键入 128 字符的命令行输入
#define MAX_ARG_NR 16   // 加上命令名外， 最多支持 15 个参数




// 存储输入的命令
static char cmd_line[cmd_len] = {0};

// 用来记录当前目录,是当前目录的缓存,每次执行cd命令时会更新此内容 
char cwd_cache[64] = {0};

// 输出命令提示符
void print_prompt(void) {
    // 并没有实现用户管理， 用代表用户的 $ 即可
    printf("[husharp@HuSharp_OS %s]$ ", cwd_cache);
}


// 从键盘缓冲区中最多读入count个字节到buf。
static void readline(char* buf, int32_t count) {

}

// 简单的 shell
void my_shell(void) {
    cwd_cache[0] = '/';



    while(1) {
        
        print_prompt();
        continue;
    }
    // panic("my_shell: should not be here!");
}