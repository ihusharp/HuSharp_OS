#include "buildin_cmd.h"
#include "assert.h"
#include "dir.h"
#include "fs.h"
#include "global.h"
#include "shell.h"
#include "stdio.h"
#include "string.h"
#include "syscall.h"


// 路径输入是在用户态进行转换
// 实现相对、 绝对路径
// 当路径输入开头为 / 时， 系统认为是绝对路径
// 否则系统认为相对路径
#define MAX_PATH_LEN 512
extern char final_path[MAX_PATH_LEN];      // 用于洗路径时的缓冲

// 将路径old_abs_path中的..和.转换为实际路径后存入new_abs_path
// new_abs_path 绝对不包含 . 和 ..
// ../sasa/../a  从左至右解析 old_abs_path  若遇到 .. 便将 new_abs_path 的最后一层去掉
static void wash_path(char* old_abs_path, char* new_abs_path) {
    assert(old_abs_path[0] == '/');
    char name[MAX_FILE_NAME_LEN] = {0};// 存储各个目录
    char* sub_path = old_abs_path;

    // eg 解析 /a/b/c  namestore 存储 "a"  返回 /b/c
    // 将最上层路径解析出来， 存储到 name_store 中
    sub_path = path_parse(sub_path, name);// 得到解析的最上层路径
    // 怎样才为 0 ？
    // 不可能是由于为 空 导致的 为 0 -----> 因为函数之前的 assert 会报错
    // 只有可能是： 由 一个或多个 / 组成
    if(name[0] == 0) {// 
        new_abs_path[0] = '/';
        new_abs_path[1] = 0;
        return;
    }
    new_abs_path[0] = 0; // 避免传给new_abs_path的缓冲区不干净
    strcat(new_abs_path, "/");// 作为人为路径分隔符， 作为while第一次找 / 时不异常， 保证通用性

    // 现在对 .. 和  . 进行判断
    while(name[0]) {
        // 首先从右至左判断是否为  ..
        if(!strcmp("..", name)) {//成功返回 0
            // 找到从此位置从右至左开始的 第一个 . 进行替代
            char* slash_ptr = strrchr(new_abs_path, '/'); 
            /*如果未到new_abs_path中的顶层目录,就将最右边的'/'替换为0,
            这样便去除了new_abs_path中最后一层路径,相当于到了上一级目录 */
            // eg 如果为 /a/b/.. 那么进行上溯, 将 此时 slash_ptr 为 b 之前的 /
            //             因此将 / 取代为 0 后， 便去除了最后的字符
            //  如果为 /a/..  那么便将 a 变为 0
            // 两种情况需要分情况进行讨论
            if(slash_ptr != new_abs_path) {
                *slash_ptr = 0;
            } else {
                *(slash_ptr + 1) = 0;
            }
        }// 至此表示 解析出的不为 ..
        // 现进行其他字符判断  是否为 . ? 
        else if(strcmp(".", name)) {//如果解析出的不是 . ，就将 name 拼接到 new 上
            if (strcmp(new_abs_path, "/")) { // 如果new_abs_path不是"/",就拼接一个"/",此处的判断是为了避免路径开头变成这样"//"
                strcat(new_abs_path, "/");
            }
            strcat(new_abs_path, name);            
        } // 否则即 解析出为 '.'， 那么无须进行处理、
        // 开始下一个
        memset(name, 0, MAX_FILE_NAME_LEN);
        if(sub_path) {
            sub_path = path_parse(sub_path, name);
        }
    }
}

// 为何此函数要放在这里？ 因为内建函数需要绝对路径
// 将 path 处理成不含 .. 和 . 的绝对路径,存储在 final_path
void make_clear_abs_path(char* path, char* final_path) {
    // path 是用户输入的 路径
    char abs_path[MAX_PATH_LEN] = {0};
    // 先判断是否输入的是绝对路径
    if(path[0] != '/') {// 若输入的不是绝对路径,就拼接成绝对路径
        memset(abs_path, 0, MAX_PATH_LEN);
        if(getcwd(abs_path, MAX_PATH_LEN) != NULL) {// 获取当前绝对路径
            // 若 abs_path 表示的当前目录不是根目录 
            if(!((abs_path[0] == '/') && (abs_path[1] == 0))) {
                strcat(abs_path, "/");// 此时进行拼接
            }
        }
    }
    strcat(abs_path, path);// 将用户输入的目录追加到工作目录之后形成绝对目录 abs_path
    wash_path(abs_path, final_path);
}

// pwd 的内建函数
void buildin_pwd(uint32_t argc, char** argv) {
    if(argc != 1) {
        printf("pwd: no argument support!\n");
        return;
    } else {
        if(NULL != getcwd(final_path, MAX_PATH_LEN)) {
            printf("%s\n", final_path);
        } else {
            printf("pwd: get current work directory failed!\n");
        }
    }
}

// cd 的内建函数
char* buildin_cd(uint32_t argc, char** argv) {
    if(argc > 2) {
        printf("cd: only support 1 argument!\n");
        return NULL;
    }
    // 若是没有参数， 那么直接 cd 到根目录
    if(argc == 1) {
        final_path[0] = '/';
        final_path[1] = 0;// 终止
    } else {//表示有输入路径， 需要得到转换为绝对路径
        make_clear_abs_path(argv[1], final_path);
    }
    // 修改当前工作目录为 final_path
    if(chdir(final_path) == -1) {
        printf("cd: no such directory! %s\n", final_path);
        return NULL;
    }
    return final_path;
}

// ls 的内建函数
// 目前只支持-h -l两个选项
void buildin_ls(uint32_t argc, char** argv) {
    char* pathname = NULL;
    uint32_t argc_idx = 1;// 跨过 argv[0] (即 ls)
    uint32_t arg_path_nr = argc - 1;
    while(argc_idx < argc) {
        if(argv[argc_idx][0] = '-') {// 如果参数是选项， 那么首字母是 -
            if(!strcmp("-l", argv[argc_idx])) {// -l
                
            } else if(!strcmp("-lh", argv[argc_idx])) {// -h
                printf("usage: -l list all information about the file.\n-h-h for help\nlist all files in the current dirctory if no option\n");
                return;
            } else {
                printf("ls: invalid option %s\nTry 'ls -h' for more information!\n");
                return;
            }
        } else {// ls 的路径参数
            if(arg_path_nr == 0) {
                pathname = argv[argc_idx];
                arg_path_nr = 1;
            } else {
                printf("ls: only support one path!\n");
                return;
            }
        } 
    }
    return;
}

// ps 命令的内建函数
// 显示任务列表
void buildin_ps(uint32_t argc, char** argv) {
    if(argc = -1) {
        printf("ps: no argument support!\n");
        return;
    }
    ps();
}

// clear 的内建函数
void buildin_clear(uint32_t argc, char** argv) {
    if(argc != 1) {
        printf("clear: no argument support!\n");
        return;
    }
    clear();
}

// mkdir 的内建函数
// -1 表示失败， 0 表示成功
int32_t buildin_mkdir(uint32_t argc, char** argv) {
    int32_t ret = -1;
    if (argc != 2) {
        printf("mkdir: only support 1 argument!\n");
    } else {
        make_clear_abs_path(argv[1], final_path);
        // 若创建的不是根目录
        if(strcmp("/", final_path)) {
            if(mkdir(final_path) == 0) {
                ret = 0;
            } else {
                printf("mkdir: create directory %s failed.\n", argv[1]);
            }
        }
    }
    return ret;
}

// rmdir 的内建函数
// rmdir 删除目录
int32_t buildin_rmdir(uint32_t argc, char** argv) {
    int32_t ret = -1;
    if(argc != 2) {
        printf("rmdir: only support 1 argument!\n");
    } else {
        make_clear_abs_path(argv[1], final_path);
        // 若删除的不是根目录
        if(strcmp("/", final_path)) {
            if(rmdir(final_path) == 0) {
                ret = 0;// 成功
            } else {
                printf("rmdir: remove directory %s failed.\n", argv[1]);
            }
        }
    }
    return ret;
}

// rm 命令内建函数
// rm 删除文件
int32_t buildin_rm(uint32_t argc, char** argv) {
        int32_t ret = -1;
    if(argc != 2) {
        printf("rmdir: only support 1 argument!\n");
    } else {
        make_clear_abs_path(argv[1], final_path);
        // 若删除的不是根目录
        if(strcmp("/", final_path)) {
            if(unlink(final_path) == 0) {
                ret = 0;// 成功
            } else {
                printf("rm: delete file %s failed.\n", argv[1]);
            }
        }
    }
    return ret;
}


