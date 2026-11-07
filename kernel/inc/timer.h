#ifndef DESOS_TIMER_H
#define DESOS_TIMER_H

#include <stdint.h>

// B.7 定时器：把 B.6 的 PIT 通道 0 包装成正式的 tick 源。
// 每个 tick 递减时间片，到点向调度器请求一次重新调度。
void     timer_init(uint32_t hz);
void     timer_on_tick(void);
uint64_t timer_ticks(void);
uint32_t timer_hz(void);

#endif // DESOS_TIMER_H
