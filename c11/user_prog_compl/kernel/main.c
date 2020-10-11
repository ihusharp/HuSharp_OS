#include "print.h"
#include "init.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"
#include "process.h"


// 由于用户进程不能访问 特权级为 0 的显存段，因此需要调用高级函数进行输出
void u_prog_a(void);
void u_prog_b(void);
void k_thread_HuSharp_1(void* args);
void k_thread_HuSharp_2(void* args);
int test_var_a = 0, test_var_b = 0;

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
    thread_start("k_thread_HuSharp_1", 31, k_thread_HuSharp_1, "agrA ");
    thread_start("k_thread_HuSharp_2", 31, k_thread_HuSharp_2, "agrB ");
    // thread_start("k_thread_HuSharp_3", 20, k_thread_HuSharp_3, "agrC 20 ");
    process_execute(u_prog_a, "user_prog_a");
    process_execute(u_prog_b, "user_prog_b");
    

    intr_enable();// 打开时钟中断

    while(1);
    return 0;
}

void k_thread_HuSharp_1(void* args) {
    char* para = args;
    while(1) {
        console_put_str(" v_a:0x");
        console_put_int(test_var_a);
    }
}

void k_thread_HuSharp_2(void* args) {
    char* para = args;
    while(1) {
        console_put_str(" v_b:0x");
        console_put_int(test_var_b);
    }
}
/* 测试用户进程 */
void u_prog_a(void) {
    while(1) {
        test_var_a++;
    }
}

/* 测试用户进程 */
void u_prog_b(void) {
    while(1) {
        test_var_b++;
    }
}