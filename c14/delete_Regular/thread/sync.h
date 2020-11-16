#ifndef __THREAD_SYNC_H
#define __THREAD_SYNC_H
#include "kernel/list.h"
#include "stdint.h"
#include "thread.h"

// 信号量
struct semaphore {
    uint8_t value;// 信号量的值，会有初始值
    struct list waiters;// 记录此信号量上阻塞的所有线程
};

// 锁结构
struct lock {
    struct task_struct* holder; //锁的持有者
    struct semaphore semaphore; // 用二元信号量来实现锁
    // 由于有时候可能在还未释放锁之前，会再次调用申请锁的函数
    // 因此需要记录是否之前申请过锁
    uint32_t holder_repeat_nr;  // 锁的持有者重复申请锁的次数
};
    
/*******   函数实现    ***************/
void sema_init(struct semaphore* psema, uint8_t value); 
void sema_down(struct semaphore* psema);
void sema_up(struct semaphore* psema);
void lock_init(struct lock* plock);
void lock_acquire(struct lock* plock);
void lock_release(struct lock* plock);

#endif