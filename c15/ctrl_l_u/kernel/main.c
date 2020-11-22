#include "console.h"
#include "dir.h"
#include "fs.h"
#include "init.h"
#include "interrupt.h"
#include "memory.h"
#include "print.h"
#include "process.h"
#include "stdio.h"
#include "string.h"
#include "syscall-init.h"
#include "syscall.h"
#include "thread.h"
#include "shell.h"
#include "assert.h"

void init(void);


int main(void)
{
    put_str("I am kernel!\n");
    init_all();

//-------------------------------------------------------
    //------------------------- 下面开始 init 进程
//-------------------------------------------------------
    cls_screen();
    printf("                                                            _  \n");    
    printf("          _   _      _ _    __        __         _     _   | |\n");
    printf("         | | | | ___| | | __\\ \\      / /__  _ __| | __| |  | |\n");
    printf("         | |_| |/ _ \\ | |/ _ \\ \\ /\\ / / _ \\| '__| |/ _` |  | |\n");
    printf("         |  _  |  __/ | | (_) \\ V  V / (_) | |  | | (_| |  |_|\n");
    printf("         |_| |_|\\___|_|_|\\___/ \\_/\\_/ \\___/|_|  |_|\\__,_|  [_]\n");

    printf("\n");
    console_put_str("[husharp@HuSharp_OS /]$ ");
    

    while (1)
        ;
    return 0;
}


// init 进程
void init(void) {
    uint32_t ret_pid = fork();
    if(ret_pid) {//为父进程
        while(1);
    } else {
        my_shell();
    }
    panic("init: should not be here");
}
