#ifndef DESOS_SCHED_H
#define DESOS_SCHED_H

#include <stdint.h>
#include "thread.h"

// B.7 调度器：round-robin 就绪队列
//   - 协作式：SYS_YIELD / SYS_EXIT 通过 sched_request() 请求让出；
//   - 抢占式：PIT tick 到期时 timer_on_tick() 调 sched_request()；
//   - 真正的上下文切换只在「中断返回路径」发生（见 idt_stubs.asm），
//     这样所有线程被挂起时的栈布局完全一致，切换后可直接 iretq。

void     sched_init(void);                         // 把当前执行流包成 main 线程
void     sched_enable(void);                       // 允许抢占（自检开始前才打开）
void     sched_request(void);                      // 置 need_resched
void     sched_enqueue(struct thread *t);
struct thread *sched_current(void);
uint64_t sched_switch_count(void);
uint64_t sched_switch_from_irq(uint64_t current_rsp);  // 由 asm 调用，返回新 rsp

#endif // DESOS_SCHED_H
