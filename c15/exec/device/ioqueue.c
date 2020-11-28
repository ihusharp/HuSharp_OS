#include "ioqueue.h"
#include "interrupt.h"
#include "global.h"
#include "debug.h"


// 初始化 io 队列
void ioqueue_init(struct ioqueue* ioq) {
    lock_init(&ioq->lock);
    ioq->producer = ioq->consumer = NULL;   // 生产者消费者置为 NULL
    ioq->head = ioq->tail = 0;  // 头尾指针指向缓冲区数组第 0 个位置
}

// 返回pos在缓冲区中的下一个位置值 
static int32_t next_pos(int32_t pos) {
    return (pos+1) % bufsize;
}

// 判断队列已满
bool ioq_full(struct ioqueue* ioq) {
    ASSERT(intr_get_status() == INTR_OFF);
    // ? 难道不是尾？
    return next_pos(ioq->head) == ioq->tail;// 因此容量为 buf - 1
}

// 判断队列为空
bool ioq_empty(struct ioqueue* ioq) {
    ASSERT(intr_get_status() == INTR_OFF);
    return ioq->head == ioq->tail;
}

// 使当前生产者或消费者在此缓冲区上等待
static void ioq_wait(struct task_struct** waiter) {
    // 参数为 pcb 类型的二级指针, 即为 pcb 指针的地址
    ASSERT(*waiter == NULL && waiter != NULL);
    *waiter = running_thread();// 将 当前线程 记录在 waiter 指向的指针中
    // *waiter 相当于 ioq->consumer
    thread_block(TASK_BLOCKED);//当前线程睡眠
}

// 唤醒waiter
static void wakeup(struct task_struct** waiter) {
    ASSERT(*waiter != NULL);
    thread_unblock(*waiter);
    *waiter = NULL;
}

// 我们对缓冲器操作的数据单元为 1 字节
// 消费者从ioq队列中获取一个字符，从队尾获取
char ioq_getchar(struct ioqueue* ioq) {
    ASSERT(intr_get_status() == INTR_OFF);
    // 先循环判断队列是否为空
    // 若为空，需要进行休眠，并唤醒生产者
    // 在休眠时，要告诉生产者需要唤醒的对象是谁，即 ioq->consumer为当前线程
    // 至于采用循环的原因，是由于可能在唤醒时，又被其他消费者取走缓冲区，导致为空
    while(ioq_empty(ioq)) {
        lock_acquire(&ioq->lock);// 获取锁
        ioq_wait(&ioq->consumer);
        lock_release(&ioq->lock);//释放锁
    }
    // 至此，缓冲区不为空
    char byte = ioq->buf[ioq->tail];// 取出缓冲区一个字节
    ioq->tail = next_pos(ioq->tail);// 将尾指针移到下一个位置

    if(ioq->producer != NULL) {
        wakeup(&ioq->producer);// 唤醒生产者
    }
    return byte;
}

// 生产者往ioq队列中写入一个字符byte  接受两个参数
void ioq_putchar(struct ioqueue* ioq, char byte) {
    ASSERT(intr_get_status() == INTR_OFF);

    while(ioq_full(ioq)) {
        lock_acquire(&ioq->lock);// 获取锁
        ioq_wait(&ioq->producer);
        lock_release(&ioq->lock);//释放锁
    }
    ioq->buf[ioq->head] = byte; //写入一个字节
    ioq->head = next_pos(ioq->head);// 头指针移到下一个

    if(ioq->consumer != NULL) {
        wakeup(&ioq->consumer);//唤醒消费者
    }
}