#include "shell.h"
#include "assert.h"
#include "buildin_cmd.h"
#include "file.h"
#include "global.h"
#include "stdint.h"
#include "stdio.h"
#include "string.h"
#include "syscall.h"

#define MAX_ARG_NR 16   // 加上命令名外， 最多支持 15 个参数


// 存储输入的命令
static char cmd_line[MAX_PATH_LEN] = { 0 };
char final_path[MAX_PATH_LEN] = { 0 }; // 用于洗路径时的缓冲

// 用来记录当前目录,是当前目录的缓存,每次执行cd命令时会更新此内容 
char cwd_cache[64] = {0};

// 输出命令提示符
void print_prompt(void) {
    // 并没有实现用户管理， 用代表用户的 $ 即可
    printf("[husharp@HuSharp_OS %s]$ ", cwd_cache);
}


// 从键盘缓冲区中最多读入count个字节到buf。
static void readline(char* buf, int32_t count) {
    assert(buf != NULL && count > 0);
    char* pos = buf;

    // 每次读取一个字符到 pos 中， 以做到即输入即显示
    // 在不出错情况下,直到找到回车符才返回
    while(read(stdin_no, pos, 1) != -1 && (pos - buf) < count) {
        switch(*pos) {
            // 找到回车或换行符后认为键入的命令结束,直接返回
            case '\n':
            case '\r':
                *pos = 0;   // 添加 cmd_line 的终止字符 0
                putchar('\n');
                return ;// 直接返回

            case '\b':// 退格符
                if(buf[0] != '\b') {//阻止删除非本次输入的信息
                    --pos;  // 回到缓冲区的上一个字符
                    putchar('\b');
                }
                break;

            /* ctrl+l 清屏 */
            case 'l' - 'a':
                /* 1 先将当前的字符'l'-'a'置为0 */
                *pos = 0;
                /* 2 再将屏幕清空 */
                clear();
                /* 3 打印提示符 */
                print_prompt();
                /* 4 将之前键入的内容再次打印 */
                printf("%s", buf);
                break;

            /* ctrl+u 清掉输入 */
            case 'u' - 'a':
                while (buf != pos) {
                    putchar('\b');
                    *(pos--) = 0;
                }
                break;

            // 非控制键 直接输出即可
            default:  // 继续 
                putchar(*pos);
                pos++;
        }
    }
    printf("readline: can`t find enter_key in the cmd_line, max num of char is 128\n");
}


// 分析字符串cmd_str中以token为分隔符的单词,将各单词的指针存入argv数组
// 返回 argc 而不用保存 argv， 是由于 采用 argv 采用全局变量， 时刻保存
static int32_t cmd_parse(char* cmd_str, char** argv, char token)
{
    assert(cmd_str != NULL);
    int32_t arg_idx = 0;
    while (arg_idx < MAX_ARG_NR) {
        argv[arg_idx] = NULL;
        arg_idx++;
    }
    char* next = cmd_str;
    
    arg_idx = 0;
    /* 外层循环处理整个命令行 */
    while (*next) {
        /* 去除命令字或参数之间的空格 */
        while (*next == token) {
            next++;
        }
        /* 处理最后一个参数后接空格的情况,如"ls dir2 " */
        if (*next == 0) {
            break;
        }
        argv[arg_idx] = next;

        /* 内层循环处理命令行中的每个命令字及参数 */
        while (*next && *next != token) { // 在字符串结束前找单词分隔符
            next++;
        }

        /* 如果未结束(是token字符),使tocken变成0 */
        if (*next) {
            *next++ = 0; // 将token字符替换为字符串结束符0,做为一个单词的结束,并将字符指针next指向下一个字符
        }

        /* 避免argv数组访问越界,参数过多则返回0 */
        if (arg_idx > MAX_ARG_NR) {
            printf("cmd_parse : exceed 15 paras! \n");
            return -1;
        }
        arg_idx++;
    }
    return arg_idx;
}


char* argv[MAX_ARG_NR]; // argv必须为全局变量，为了以后exec的程序可访问参数
int32_t argc = -1;

// 简单的 shell
void my_shell(void) {
    cwd_cache[0] = '/';// 缓存 根目录
    cwd_cache[1] = 0;

    while(1) {
        print_prompt();// 输出命令提示符
        memset(cmd_line, 0, MAX_PATH_LEN);
        memset(cmd_line, 0, MAX_PATH_LEN);
        readline(cmd_line, MAX_PATH_LEN);// 获取用户输入
        if(cmd_line[0] == 0) {// 若只是输入回车符
            continue;
        }
        argc = -1;
        argc = cmd_parse(cmd_line, argv, ' ');
        if (argc == -1) {
            printf("num of arguments exceed %d\n", MAX_ARG_NR);
            continue;
        }

        char buf[MAX_PATH_LEN] = { 0 };
        int32_t arg_idx = 0;
        while (arg_idx < argc) {
            make_clear_abs_path(argv[arg_idx], buf);
            printf("%s -> %s\n", argv[arg_idx], buf);
            arg_idx++;
        }
        // printf("\n");
    }
    panic("my_shell: should not be here!");
}