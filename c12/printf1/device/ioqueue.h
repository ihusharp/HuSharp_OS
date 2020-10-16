#ifndef __DEVICE_IOQUEUE_H
#define __DEVICE_IOQUEUE_H
#include "stdint.h"
#include "thread.h"
#include "sync.h"

#define bufsize 64  // 队列大小为 64，但是由于头尾指针差 1 ，因此容量为 63

// 环形队列
struct ioqueue {
    // 生产者-消费者 问题，需要实现同步互斥
    struct lock lock;
    // 生产者
    struct task_struct* producer;
    // 消费者
    struct task_struct* consumer;
    // 缓冲区
    char buf[bufsize];
    int32_t head;//头指针
    int32_t tail;//尾指针
};

void ioqueue_init(struct ioqueue* ioq);
bool ioq_full(struct ioqueue* ioq);
bool ioq_empty(struct ioqueue* ioq);
char ioq_getchar(struct ioqueue* ioq);
void ioq_putchar(struct ioqueue* ioq, char byte);

#endif