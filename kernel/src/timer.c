// timer.c —— B.7 定时器（对应 ProjectRoad B.7）
// 复用 B.6 的 PIT 通道 0；每个 tick 递减时间片，到点请求重新调度。

#include "timer.h"
#include "pic.h"
#include "sched.h"
#include "usb.h"
#include <stdint.h>

#define DEFAULT_HZ 100
#define TICKS_PER_SLICE 1       // 每个 tick 一个时间片，便于验证抢占

static uint64_t ticks;
static uint32_t hz;
static uint32_t slice_left;

void timer_init(uint32_t frequency) {
    ticks = 0;
    hz = frequency ? frequency : DEFAULT_HZ;
    slice_left = TICKS_PER_SLICE;
    pit_init(hz);
}

void timer_on_tick(void) {
    ticks++;
    usb_poll();            // C.6：非阻塞处理 xHCI 事件环（HID 中断传输完成回调）
    if (slice_left > 0) slice_left--;
    if (slice_left == 0) {
        sched_request();
        slice_left = TICKS_PER_SLICE;
    }
}

uint64_t timer_ticks(void) { return ticks; }
uint32_t timer_hz(void)    { return hz; }
