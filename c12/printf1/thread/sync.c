#include "sync.h"
#include "kernel/list.h"
#include "global.h"
#include "debug.h"
#include "interrupt.h"

// 初始化信号量
void sema_init(struct semaphore* psema, uint8_t value) {
    psema->value = value;
    list_init(&psema->waiters);
}

// 初始化锁
void lock_init(struct lock* plock) {
    plock->holder = NULL;
    plock->holder_repeat_nr = 0;//将重复量置为0
    // 本系统采用二元信号量
    sema_init(&plock->semaphore, 1);// 信号量初始化为 1
}

// 信号量的 down 操作
void sema_down(struct semaphore* psema) {
    enum intr_status old_status = intr_disable();// 先关中断 保证原子性
    // 用 while 保证一直对信号量进行判断
    while(psema->value == 0) {//循环判断value 要是一直为 0 便一直堵塞
        // 开始阻塞之前，不应该在该锁的等待队列中
        ASSERT(!elem_find(&psema->waiters, &running_thread()->general_tag));

        if (elem_find(&psema->waiters, &running_thread()->general_tag)) {
            PANIC("sema_down: thread bloked has been in block_waiter_list\n");
        }
        // 将当前线程加入到该锁的等待队列中
        list_append(&psema->waiters, &running_thread()->general_tag);
        // 调用阻塞函数，去调度其他线程
        // 当调度线程为该锁的持有者时，运行完之后，就会将该 堵塞进程唤醒
        thread_block(TASK_BLOCKED);//堵塞态，一直等待到唤醒
        // 醒来后，由于是 while 因此会继续进行判断，发现此时为 1 就跳出循环 进行-1
    }
    // 若 value 为1 ，或者是被唤醒后，即此时获取了锁
    psema->value--;
    ASSERT(psema->value == 0);
    // 恢复之前的中断状态
    intr_set_status(old_status);
}

// 信号量中的 up 操作
void sema_up(struct semaphore* psema) {
    // 关中断
    enum intr_status old_status = intr_disable();
    ASSERT(psema->value == 0);// 此时应该为 0 ，进行唤醒
    if(!list_empty(&psema->waiters)) {//若锁的等待数组不为 0 ，需要唤醒（由于是二元信号量，唤醒一个即可
        struct task_struct* blocked_thread = elem2entry(struct task_struct, general_tag, list_pop(&psema->waiters));
        // 调用唤醒函数
        thread_unblock(blocked_thread);
    }// 所谓唤醒，并非是直接开始执行，而是改变 thread 的 stat 使其加入到就绪队列中。
    // 并且为 push 加入，优先进行调度 
    // 至此 锁的等待队列中没有在等待线程了
    psema->value++;// 归还锁
    ASSERT(psema->value == 1);
    // 进行开中断
    intr_set_status(old_status);
}

// 锁的获取函数
// 主要值得注意的是，不能重复申请锁（以免变为死锁——即自己等待自己释放锁）
// 主要判断点在于 锁 中的 holder_repeat_nr 变量
void lock_acquire(struct lock* plock) {
    if (plock->holder != running_thread()) {//持有者不为当前者
        sema_down(&plock->semaphore);//对信号量执行 P 操作，可能会阻塞
        plock->holder = running_thread();// 此时获取到锁
        // 之前还未获取到锁
        ASSERT(plock->holder_repeat_nr == 0);
        // 此时表示第一次获取到锁
        plock->holder_repeat_nr = 1;
    } else {//持有者是当前者，未避免死锁，拒绝再获取锁，将申请次数++，直接返回即可
        plock->holder_repeat_nr++;
    }
}

// 释放锁函数 释放参数中的锁
void lock_release(struct lock* plock) {
    // 当前线程应该为锁的持有者
    ASSERT(plock->holder == running_thread());
    if (plock->holder_repeat_nr > 1) {//说明多次申请该锁，还不能进行释放
        plock->holder_repeat_nr--;// 因为要嵌套返回，且之前获取锁并未操作
        return;
    }
    ASSERT(plock->holder_repeat_nr == 1);

    // 改 plock 需要在 V 操作前面执行
    // 因为 释放锁的操作并未关中断
    // 若将 V 放在 holder 置为 NULL 之前，那么可能出现如下情况：
    // 当前线程刚刚执行完 V ，还未置为 NULL 便被调度为新线程
    // 新线程将 holder 置为 新线程的 PCB
    // 此时又被调度为 置为 NULL  ，易知不得行
    plock->holder = NULL;
    plock->holder_repeat_nr = 0;
    sema_up(&plock->semaphore);// 信号量的 V 操作
}