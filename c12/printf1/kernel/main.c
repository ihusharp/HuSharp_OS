#include "print.h"
#include "init.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"
#include "process.h"
#include "syscall.h"
#include "syscall-init.h"
#include "stdio.h"

// 由于用户进程不能访问 特权级为 0 的显存段，因此需要调用高级函数进行输出
void u_prog_a(void);
void u_prog_b(void);
void k_thread_HuSharp_1(void* args);
void k_thread_HuSharp_2(void* args);
int prog_a_pid = 0, prog_b_pid = 0; // 全局变量存储pid值

int main(void) {
    put_str("I am kernel!\n");
    init_all();
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
    process_execute(u_prog_a, "user_prog_a");
    process_execute(u_prog_b, "user_prog_b");
    
    intr_enable();
    console_put_str(" main_pid:0x");
    console_put_int(sys_getpid());
    console_put_char('\n');
    
    thread_start("k_thread_HuSharp_1", 31, k_thread_HuSharp_1, "agrA ");
    thread_start("k_thread_HuSharp_2", 31, k_thread_HuSharp_2, "agrB ");

    while(1);
    return 0;
}

void k_thread_HuSharp_1(void* args) {
    char* para = args;
    console_put_str(" thread_a_pid:0x");
    console_put_int(sys_getpid());
    console_put_char('\n');
    while(1);
}

void k_thread_HuSharp_2(void* args) {
    char* para = args;
    console_put_str(" thread_b_pid:0x");
    console_put_int(sys_getpid());
    console_put_char('\n');
    while(1);
}
/* 测试用户进程 */
void u_prog_a(void) {
    char* name = "prog_a";
    printf("I am %s, pid:0x%x%c", name, getpid(), '\n');
    while(1);
}

/* 测试用户进程 */
void u_prog_b(void) {
    char* name = "prog_b";
    printf("I am %s, pid:0x%x%c", name, getpid(), '\n');
    while(1);
}