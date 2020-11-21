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

// 由于用户进程不能访问 特权级为 0 的显存段，因此需要调用高级函数进行输出
void u_prog_a(void);
void u_prog_b(void);
void k_thread_HuSharp_1(void*);
void k_thread_HuSharp_2(void*);
int prog_a_pid = 0, prog_b_pid = 0; // 全局变量存储pid值


void init(void);


int main(void)
{
    put_str("I am kernel!\n");
    init_all();
    // while(1);
    //ASSERT(1 == 2);
    // asm volatile("sti");    // 打开中断 即将 EFLAGS 的 IF置为 1

    // 进行内存分配
    /* 内核物理页分配 
    void* addr = get_kernel_pages(3);
    put_str("\n get_kernel_page start vaddr is: ");
    put_int((uint32_t) addr);
    put_str("\n"); 
    */

    // 线程演示
    // thread_start("k_thread_HuSharp_3", 20, k_thread_HuSharp_3, "agrC 20 ");
    // 目前还未实现为用户进程打印字符的系统调用，因此需要内核线程帮忙打印
    // process_execute(u_prog_a, "user_prog_a");
    // process_execute(u_prog_b, "user_prog_b");

    // intr_enable();
    // console_put_str(" main_pid:0x");
    // console_put_int(sys_getpid());
    // console_put_char('\n');
    // intr_enable();
    //--------------------------------------------------进程
    // process_execute(u_prog_a, "u_prog_a");
    // process_execute(u_prog_b, "u_prog_b");
    // thread_start("k_thread_HuSharp_1", 31, k_thread_HuSharp_1, "agrA ");
    // thread_start("k_thread_HuSharp_2", 31, k_thread_HuSharp_2, "agrB ");
    //-------------------------------------------
    // sys_open("/file1", OP_CREAT);
    //---------------------写入文件
    // uint32_t fd = sys_open("/file1", OP_RDWR);
    // printf("fd:%d\n", fd);
    // sys_write(fd, "hello world!\n", 13);
    // sys_close(fd);
    // printf("fd:%d close now\n", fd);
    //--------------------读取文件

    //-------------------- 通过改变文件打开偏移量进行文件查看
    // uint32_t fd = sys_open("/file1", OP_RDWR);
    // printf("open /file1, fd: %d\n", fd);
    // char buf[64] = { 0 };
    // int read_bytes = sys_read(fd, buf, 20);
    // printf("1_ read %d bytes:%s\n", read_bytes, buf);

    // memset(buf, 0, 64);
    // read_bytes = sys_read(fd, buf, 6);
    // printf("2_ read %d bytes:%s\n", read_bytes, buf);

    // memset(buf, 0, 64);
    // read_bytes = sys_read(fd, buf, 6);
    // printf("3_ read %d bytes:%s\n", read_bytes, buf);

    // printf("________  SEEK_SET 0  ________\n");
    // sys_lseek(fd, 0, SEEK_SET);
    // memset(buf, 0, 64);
    // read_bytes = sys_read(fd, buf, 24);
    // printf("4_ read %d bytes:\n%s", read_bytes, buf);

    // read_bytes = sys_read(fd, buf, 26);
    // printf("4_ read %d bytes:\n%s", read_bytes, buf);
    // sys_close(fd);
    //-------------------- ---------------------------------

    //----------------删除文件操作 unlink
    // printf("/file1 delete %s!\n", sys_unlink("/file1") == 0 ? "done" : "fail");
    //--------------------

    //----------------创建目录 mkdir
    // printf("/dir1/subdir1 create %s!\n", sys_mkdir("/dir1/subdir1") == 0 ? "done" : "fail");
    // printf("/dir1 create %s!\n", sys_mkdir("/dir1") == 0 ? "done" : "fail");
    // printf("now, /dir1/subdir1 create %s!\n", sys_mkdir("/dir1/subdir1") == 0 ? "done" : "fail");
    // int fd = sys_open("/dir1/subdir1/file2", OP_CREAT | OP_RDWR);
    // if (fd != -1) {
    //     printf("/dir1/subdir1/file2 create done!\n");
    //     sys_write(fd, "Catch me if you can!\n", 21);
    //     sys_lseek(fd, 0, SEEK_SET);
    //     char buf[32] = { 0 };
    //     sys_read(fd, buf, 21);
    //     printf("/dir1/subdir1/file2 says:\n%s", buf);
    //     sys_close(fd);
    // }
    // --------------------------------

    // --------------- 目录打开与关闭
    // struct dir* p_dir = sys_opendir("/dir1/subdir1");
    // if (p_dir) {
    //     printf("/dir1/subdir1 open done!\n");
    //     if (sys_closedir(p_dir) == 0) {
    //         printf("/dir1/subdir1 close done!\n");
    //     } else {
    //         printf("/dir1/subdir1 close fail!\n");
    //     }
    // } else {
    //     printf("/dir1/subdir1 open fail!\n");
    // }
    // ---------------------------------------

    // -------------- 目录遍历目录项， 需要先打开
    // struct dir* p_dir = sys_opendir("/dir1/subdir1");
    // if (p_dir) {
    //     printf("/dir1/subdir1 open done!\n");

    //     // 开始遍历
    //     printf("content:\n");
    //     struct dir_entry* dir_e = NULL;
    //     char* type = NULL;// 表明类型
    //     while((dir_e = sys_readdir(p_dir))) {
    //         if(dir_e->f_type == HS_FT_REGULAR) {
    //             type = "Regular";
    //         } else {
    //             type = "Directory";
    //         }
    //         printf("    %s  %s\n", type, dir_e->filename);
    //     }

    //     if (sys_closedir(p_dir) == 0) {
    //         printf("/dir1/subdir1 close done!\n");
    //     } else {
    //         printf("/dir1/subdir1 close fail!\n");
    //     }
    // } else {
    //     printf("/dir1/subdir1 open fail!\n");
    // }
    // --------------------------------------------

    //------------------ 目录的删除 rmkdir
    // ---- 首先进行目录遍历
    // printf("/dir1 content before delete /dir1/subdir1:\n");
    // struct dir* p_dir = sys_opendir("/dir1/subdir1");
    // if (p_dir) {
    //     printf("/dir1/subdir1 open done!\n");

    //     // 开始遍历
    //     printf("content:\n");
    //     struct dir_entry* dir_e = NULL;
    //     char* type = NULL; // 表明类型
    //     while ((dir_e = sys_readdir(p_dir))) {
    //         if (dir_e->f_type == HS_FT_REGULAR) {
    //             type = "Regular";
    //         } else {
    //             type = "Directory";
    //         }
    //         printf("    %s  %s\n", type, dir_e->filename);
    //     }
    // } else {
    //     printf("/dir1/subdir1 open fail!\n");
    // }
    // //------ 开始进行删除
    // printf("try to delete nonempty directory /dir1/subdir1\n");
    // if (sys_rmdir("/dir1/subdir1") == -1) {
    //     printf("sys_rmdir: /dir1/subdir1 delete fail!\n");
    // }

    // printf("try to delete /dir1/subdir1/file2\n");
    // if (sys_rmdir("/dir1/subdir1/file2") == -1) {
    //     printf("sys_rmdir: /dir1/subdir1/file2 delete fail!\n");
    // }
    // if (sys_unlink("/dir1/subdir1/file2") == 0) {
    //     printf("sys_unlink: /dir1/subdir1/file2 delete done\n");
    // }

    // printf("try to delete directory /dir1/subdir1 again\n");
    // if (sys_rmdir("/dir1/subdir1") == 0) {
    //     printf("/dir1/subdir1 delete done!\n");
    // }
    // // ------ 此时再进行遍历查看
    // printf("/dir1 content after delete /dir1/subdir1:\n");
    // p_dir = sys_opendir("/dir1/subdir1");
    // if (p_dir) {
    //     printf("/dir1/subdir1 open done!\n");

    //     // 开始遍历
    //     printf("content:\n");
    //     struct dir_entry* dir_e = NULL;
    //     char* type = NULL; // 表明类型
    //     while ((dir_e = sys_readdir(p_dir))) {
    //         if (dir_e->f_type == HS_FT_REGULAR) {
    //             type = "Regular";
    //         } else {
    //             type = "Directory";
    //         }
    //         printf("    %s  %s\n", type, dir_e->filename);
    //     }

    // } else {
    //     printf("/dir1/subdir1 open fail!\n");
    // }
// --------------------------------------------------
    // // ------------- 工作目录展示
    // char cwd_buf[32] = {0};
    // sys_getcwd(cwd_buf, 32);
    // printf("cwd:%s\n", cwd_buf);
    // sys_chdir("/dir1");
    // printf("change cwd now!\n");
    // sys_getcwd(cwd_buf, 32);
    // printf("cwd:%s\n", cwd_buf);
    // ---------------------------------------------

// -----------------------------------------------------
    // ---------------------文件属性获取
    // struct stat obj_stat;
    // sys_stat("/", &obj_stat);
    // printf("/'s info:\n   i_no:%d\n   size:%d\n   filetype:%s\n",
    //     obj_stat.st_ino, obj_stat.st_size, 
    //     obj_stat.st_filetype == 2 ?"directory" : "regular");
    // sys_stat("/dir1", &obj_stat);
    // printf("/dir1's info:\n   i_no:%d\n   size:%d\n   filetype:%s\n",
    //     obj_stat.st_ino, obj_stat.st_size, 
    //     obj_stat.st_filetype == 2 ?"directory" : "regular");
// -----------------------------------------------------

//-------------------------------------------------------
    //------------------------- 下面开始 init 进程
//-------------------------------------------------------
    while (1)
        ;
    return 0;
}


// init 进程
void init(void) {
    uint32_t ret_pid = fork();
    if(ret_pid) {
        printf("I am father, my pid is %d, child pid is %d\n", getpid(), ret_pid);
    } else {
        printf("I am child, my pid is %d, ret pid is %d\n", getpid(), ret_pid);
    }
    while(1);    
}



void k_thread_HuSharp_1(void* arg)
{
    void* addr1 = sys_malloc(256);
    void* addr2 = sys_malloc(255);
    void* addr3 = sys_malloc(254);
    console_put_str(" thread_a malloc addr:0x");
    console_put_int((int)addr1);
    console_put_char(',');
    console_put_int((int)addr2);
    console_put_char(',');
    console_put_int((int)addr3);
    console_put_char('\n');

    int cpu_delay = 100000;
    while (cpu_delay-- > 0)
        ;
    sys_free(addr1);
    sys_free(addr2);
    sys_free(addr3);
    while (1)
        ;
}

void k_thread_HuSharp_2(void* arg)
{
    void* addr1 = sys_malloc(256);
    void* addr2 = sys_malloc(255);
    void* addr3 = sys_malloc(254);
    console_put_str(" thread_b malloc addr:0x");
    console_put_int((int)addr1);
    console_put_char(',');
    console_put_int((int)addr2);
    console_put_char(',');
    console_put_int((int)addr3);
    console_put_char('\n');

    int cpu_delay = 100000;
    while (cpu_delay-- > 0)
        ;
    sys_free(addr1);
    sys_free(addr2);
    sys_free(addr3);
    while (1)
        ;
}
/* 测试用户进程 */
void u_prog_a(void)
{
    void* addr1 = malloc(256);
    void* addr2 = malloc(255);
    void* addr3 = malloc(254);
    printf(" prog_a malloc addr:0x%x,0x%x,0x%x\n", (int)addr1, (int)addr2, (int)addr3);

    int cpu_delay = 100000;
    while (cpu_delay-- > 0)
        ;
    free(addr1);
    free(addr2);
    free(addr3);
    while (1)
        ;
}

/* 测试用户进程 */
void u_prog_b(void)
{
    void* addr1 = malloc(256);
    void* addr2 = malloc(255);
    void* addr3 = malloc(254);
    printf(" prog_b malloc addr:0x%x,0x%x,0x%x\n", (int)addr1, (int)addr2, (int)addr3);

    int cpu_delay = 100000;
    while (cpu_delay-- > 0)
        ;
    free(addr1);
    free(addr2);
    free(addr3);
    while (1)
        ;
}