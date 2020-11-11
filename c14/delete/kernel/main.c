#include "console.h"
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
    process_execute(u_prog_a, "u_prog_a");
    process_execute(u_prog_b, "u_prog_b");
    thread_start("k_thread_HuSharp_1", 31, k_thread_HuSharp_1, "agrA ");
    thread_start("k_thread_HuSharp_2", 31, k_thread_HuSharp_2, "agrB ");

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
    printf("/file1 delete %s!\n", sys_unlink("/file1") == 0 ? "done" : "fail");
    //--------------------
    while (1)
        ;
    return 0;
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