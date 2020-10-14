#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "debug.h"
#include "interrupt.h"
#include "print.h"
#include "memory.h"
#include "process.h"
#include "list.h"
#include "sync.h"

#define PG_SIZE 4096

// 定义主线程的 PCB，因为进入内核后，实则上一直执行的是 main 函数
struct task_struct* main_thread;    // 主线程 PCB
struct list thread_ready_list;      // 就绪队列
// 当线程不为就绪态时，会从所有线程队列中找到它
struct list thread_all_list;        // 所有线程队列
// 队列是以 elem 的形式储存在 list 队列中，因此需要一个 elem 变量来将其取出转换 
static struct list_elem* thread_tag;    // 用于保存队列中的线程节点

// 分配 pid 锁
struct lock pid_lock;   // 因为 pid 必须是唯一的，所以需要互斥

// switch_to函数的外部声明 global
extern void switch_to(struct task_struct* cur, struct task_struct* next);


// 取当前线程的 PCB 指针 
struct task_struct* running_thread(void) {
    uint32_t esp;
    asm("mov %%esp, %0" : "=g"(esp));
    // 取 esp 的前20位，PCB 的栈顶就为 0级栈
    return (struct task_struct*)(esp & 0xfffff000);
}

// 不能按照以往的函数调用， 比如 kernel_thread(function， func_arg)
// 因为 我们此处采用的是 ret 返回，而不是 call ，
// 因此需要采用存储到 kernel_thread 的栈中，存储参数和占位返回地址的方式

/* 由kernel_thread去执行function(func_arg) */
static void kernel_thread(thread_func* function, void* func_arg) {
    // 由于线程的运行是由调度器中断调度，进入中断后，处理器会自动关中断。
    // 执行 function 前开中断，避免之后的时钟中断被屏蔽，从而无法调度其他线程
    intr_enable();
    function(func_arg); // 调用 function
}

// 分配 pid --->需要在 thread 初始化期间进行，因此放到 init_thread 中
static _pid_t allocate_pid(void) {
    // 利用全局变量进行 pid 标记
    static _pid_t next_pid = 0;
    lock_acquire(&pid_lock);
    next_pid++;
    lock_release(&pid_lock);
    return next_pid;
}


// 初始化线程栈thread_stack,将待执行的函数和参数放到thread_stack中相应的位置 
void thread_create(struct task_struct* pthread, thread_func function, void* func_arg) {
    // 由于 init_thread 中，已经指向最顶端了
    // 先预留中断使用栈的空间，可见 thread.h 中定义的结构
    // 中断栈用于保存中断时的上下文，实现用户进程时，初始值也会放在中断栈
    pthread->self_kstack -= sizeof(struct intr_stack);

    // 再留出线程栈空间
    // 用于存储在中断处理程序中、任务切换前后的上下文
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
    pthread->pid = allocate_pid();
    strcpy(pthread->name, name);
    // 将主函数也封装为一个线程，且由于其一直运行，因此状态赋为 Running
    if (pthread == main_thread) {
        pthread->status = TASK_RUNNING;
    } else {
        pthread->status = TASK_READY;
    }

    
    // 此处是为演习，因此直接将 状态置为 running
    pthread->priority = prio;
    pthread->ticks = prio;
    pthread->elapsed_ticks = 0; //表示还未执行过
    pthread->pgdir = NULL;  //线程没有自己的地址空间
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
    // 至此 线程得到初始化和创建后，需要加入到就绪队列和全局队列中
    
    // 首先需要判断不在就绪队列中
    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    // 加入就绪队列
    list_append(&thread_ready_list, &thread->general_tag);
    // 首先需要判断不在全局队列中
    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
    // 加入就绪队列
    list_append(&thread_all_list, &thread->all_list_tag);
    

    // 简陋版本（以后改为 switch_to
    // 作用为：开启线程
    // 由于 thread_create 中将 self_kstack 指向线程栈的最低处，现在将 self_kstack 作为栈顶
    // ret 操作时，由于 thread_create 函数中，将 eip 指向了 kernel_thread，因此 ret时，会跳转到该函数
    return thread;
}


// 将kernel中的main函数完善为主线程 
static void make_main_thread(void) {
    // 因为main线程早已运行,咱们在loader.S中进入内核时的mov esp,0xc009f000,
    // 就是为其预留了tcb,地址为0xc009e000,因此不需要通过get_kernel_page另分配一页
    // 直接 init_thread 即可
    main_thread = running_thread();// 获取当前的PCB指针
    init_thread(main_thread, "main", 31);

    // main函数是当前线程,当前线程不在thread_ready_list中
    // 只用加到 全局线程队列中
    ASSERT(!elem_find(&thread_all_list, &main_thread->all_list_tag));
    list_append(&thread_all_list, &main_thread->all_list_tag);
}


// 实现任务调度 读写就绪队列
void schedule(void) {
    ASSERT(intr_get_status() == INTR_OFF);

    struct task_struct* cur = running_thread();// 取出当前线程的PCB
    if(cur->status == TASK_RUNNING) {// 只是时间片为 0 ，而非阻塞
        ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
        list_append(&thread_ready_list, &cur->general_tag);// 队尾
        // 现将当前线程的 ticks 再次赋为 prio
        cur->ticks = cur->priority;
        cur->status = TASK_READY;
    }
    //由于还未实现 idle 线程，因此可能出现 ready_list 中无线程可调度的情况
    // 因此先进行断言 ready 队列中是否存在元素
    ASSERT(!list_empty(&thread_ready_list));
    thread_tag = NULL;  // 首先将全局变量清空
    // 将就绪进程中的第一个线程（头结点）弹出
    thread_tag = list_pop(&thread_ready_list);
    // 现在获得了 PCB 的 elem 节点，需要将其转换为 PCB
    struct task_struct* next = elem2entry(struct task_struct, general_tag, thread_tag);
    next->status = TASK_RUNNING;// 调度

    // 激活 任务页表
    process_activate(next);

    switch_to(cur, next);
}

// 当前线程将自己阻塞,标志其状态为stat
void thread_block(enum task_status stat) {
    // stat取值为TASK_BLOCKED,TASK_WAITING,TASK_HANGING 指不可运行
    ASSERT(((stat == TASK_BLOCKED) || (stat == TASK_WAITING) || (stat == TASK_HANGING)));

    enum intr_status old_status = intr_disable();// 保存中断前状态
    // 当前运行的必然为 RUNNing态
    struct task_struct* cur_thread = running_thread();// 获取当前线程的PCB
    cur_thread->status = stat;
    schedule();//进行调度
    // 待当前线程被解除阻塞后才继续运行下面的函数
    intr_set_status(old_status);
}

// 将线程 pthread 解除阻塞 唤醒
// 被阻塞的线程必须来等别人唤醒自己
void thread_unblock(struct task_struct* pthread) {
    enum intr_status old_status = intr_disable();
    ASSERT(((pthread->status == TASK_BLOCKED) || (pthread->status == TASK_WAITING) || (pthread->status == TASK_HANGING)));
    if(pthread->status != TASK_READY) {
        // 在就绪队列中没有该阻塞线程
        ASSERT(!elem_find(&thread_ready_list, &pthread->general_tag));
        if (elem_find(&thread_ready_list, &pthread->general_tag)) {
            PANIC("thread_unblock: blocked thread in ready_list\n");
        }
        list_push(&thread_ready_list, &pthread->general_tag);//push 让其优先被调度
        pthread->status = TASK_READY;
    }
    intr_set_status(old_status);
}


// 初始化线程环境
void thread_environment_init(void) {
    put_str("thread_init start!\n");
    list_init(&thread_ready_list);
    list_init(&thread_all_list);
    lock_init(&pid_lock);
    // 将 main 函数创建为 线程
    make_main_thread();
    put_str("thread_init done!\n");
}