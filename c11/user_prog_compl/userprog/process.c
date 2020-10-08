#include "process.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"    
#include "list.h"    
#include "tss.h"    
#include "interrupt.h"
#include "string.h"
#include "console.h"

extern void intr_exit(void);

// 用户进程基于线程实现
// 创建用户进程初始上下文信息
// 参数 filename_ 表示用户程序的名称
// 至于 为啥是 filename_，是由于用户程序必然是通过文件系统加载进来的，因此就用文件名代指用户程序名
void start_process(void* filename_) {
    // 由于还未实现文件系统，因此采用普通函数代指用户程序
    void* function = filename_;
    struct task_struct* cur = running_thread();
    cur->self_kstack += sizeof(struct thread_stack);//为引用 intr_stack 栈，需要先跨越 thread_stack栈
    // 为啥不直接从 pcb 顶端开始访问 intr_stack?而是得地址从低往高访问？
    // 因为 结构体成员是按照成员顺序，由低到高放置
    // 而 栈是由高地址向低处延伸，因此指针 proc_stack 放到低地址，往上访问，才和成员放置一样
    struct intr_stack* proc_stack = (struct intr_stack*)cur->self_kstack;
    proc_stack->edi = proc_stack->esi = proc_stack->ebp = proc_stack->esp_dummy = 0;
    proc_stack->ebx = proc_stack->edx = proc_stack->ecx = proc_stack->eax = 0;
    proc_stack->gs = 0; // os 不允许 用户进程访问显存段，因此置为 0
    
//     uint32_t gs;
//     uint32_t fs;
//     uint32_t es;
//     uint32_t ds;

// /* 以下由cpu从低特权级进入高特权级时压入 */
//     uint32_t err_code;		 // err_code会被压入在eip之后
//     void (*eip) (void);
//     uint32_t cs;
//     uint32_t eflags;
//     void* esp;
//     uint32_t ss;
}

void process_execute(void* filename, char* name);

void process_activate(struct task_struct* p_thread);
void page_dir_activate(struct task_struct* p_thread);
uint32_t* create_page_dir(void);
void create_user_vaddr_bitmap(struct task_struct* user_prog);

