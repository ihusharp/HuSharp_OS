#include "assert.h"
#include "console.h"
#include "dir.h"
#include "fs.h"
#include "init.h"
#include "interrupt.h"
#include "memory.h"
#include "print.h"
#include "process.h"
#include "shell.h"
#include "stdio.h"
#include "string.h"
#include "syscall-init.h"
#include "syscall.h"
#include "thread.h"

void init(void);

int main(void)
{
    put_str("I am kernel!\n");
    init_all();

    // -----------------------------------
    // 写入应用程序
    // while(1);
    uint32_t file_size = 10316;
    uint32_t sec_cnt = DIV_ROUND_UP(file_size, 512);
    struct disk* sda = &channels[0].devices[0];
    void* prog_buf = sys_malloc(file_size);
    ide_read(sda, 300, prog_buf, sec_cnt);
    int32_t fd = sys_open("/prog_arg", OP_CREAT | OP_RDWR);
    if (fd != -1) {
        if (sys_write(fd, prog_buf, file_size) == -1) {
            printk("file write error!\n");
            while (1)
                ;
        }
    }
    // -----------------------------------

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
void init(void)
{
    uint32_t ret_pid = fork();
    if (ret_pid) { //为父进程
        while (1)
            ;
    } else {
        my_shell();
    }
    panic("init: should not be here");
}
