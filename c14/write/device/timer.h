#ifndef __DEVICE_TIME_H
#define __DEVICE_TIME_H
#include "stdint.h"
void timer_init(void);
// 简易休眠函数
void mtime_sleep(uint32_t m_seconds);
#endif

