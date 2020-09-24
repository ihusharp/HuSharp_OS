#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "memory.h"

#define PG_SIZE 4096

// 不能按照以往的函数调用， 比如 kernel_thread(function， func_arg)
// 因为 我们此处采用的是 ret 返回，而不是 call ，
// 因此需要采用存储到 kernel_thread 的栈中，存储参数和占位返回地址的方式


/* 由kernel_thread去执行function(func_arg) */
static void kernel_thread(thread_func* function, void* func_arg) {
    function(func_arg); // 调用 function
}

// 初始化线程栈thread_stack,将待执行的函数和参数放到thread_stack中相应的位置 
void thread_create(struct task_struct* pthread, thread_func function, void* func_arg) {
    // 由于 init_thread 中，已经指向最顶端了
    // 先预留中断使用栈的空间，可见 thread.h 中定义的结构
    // 中断栈用于保存中断时的上下文，实现用户进程时，初始值也会放在中断栈
    pthread->self_kstack -= sizeof(struct intr_stack);

    // 再留出线程栈空间
    pthread->self_kstack -= sizeof(struct thread_stack);

    // 定义线程栈指针
    struct thread_stack* kthread_stack = (struct thread_stack*) pthread->self_kstack;

    // 为 kernel_thread 中 能调用 function 做准备
    // kernel_thread 是第一个函数， eip直接指向它，然后再调用其他的 function
    kthread_stack->eip = kernel_thread;
    kthread_stack->function = function;
    kthread_stack->func_arg = func_arg;
    // 初始化为 0 ，在还未执行函数前，寄存器不应该有值
    kthread_stack->ebp = kthread_stack->ebx = \
        kthread_stack->edi = kthread_stack->esi = 0; 
}


// 初始化线程基本信息
void init_thread(struct task_struct* pthread, char* name, int prio) {
    // 
    memset(pthread, 0, sizeof(*pthread));

    strcpy(pthread->name, name);
    // 此处是为演习，因此直接将 状态置为 running
    pthread->status = TASK_RUNNING;
    pthread->priority = prio;
    pthread->stack_magic = 0x20000611;  //自定义一个魔数
    // self_kstack 是线程自己在内核态下使用的栈顶地址，指向最顶端
    pthread->self_kstack = (uint32_t*)((uint32_t)pthread + PG_SIZE);
}

// 线程创建函数
//创建一优先级为prio的线程,线程名为name,线程所执行的函数是function(func_arg)
struct task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg) {
    // pcb 位于内核空间，包括用户进程的 pcb 也是在内核空间中
    
    // 先通过 内核物理页申请函数 申请一页
    struct task_struct* thread = get_kernel_pages(1);
    // 由于 get_kernel_page 获取的是起始位置，因此获取的是 pcb 最低地址
    // 初始化新建线程
    init_thread(thread, name, prio);
    // 创建线程
    thread_create(thread, function, func_arg);

    // 简陋版本（以后改为 switch_to
    // 作用为：开启线程
    // 由于 thread_create 中将 self_kstack 指向线程栈的最低处，现在将 self_kstack 作为栈顶
    // ret 操作时，由于 thread_create 函数中，将 eip 指向了 kernel_thread，因此 ret时，会跳转到该函数
    asm volatile("movl %0, %%esp; pop %%ebp; pop %%ebx; pop %%edi; pop %%esi; ret" : : "g"(thread->self_kstack) : "memory");
    return thread;
}