// syscall.c —— B.9 系统调用分发表（对应 ProjectRoad B.9）
//
// 入口是 IDT 向量 0x80（int 0x80，DPL=3），由 isr_common 保存现场后调用
// syscall_dispatch。返回值写回栈帧 rax，恢复寄存器时自然回到调用者。

#include "syscall.h"
#include "idt.h"
#include "serial.h"
#include "sched.h"
#include "timer.h"
#include "thread.h"
#include "kprintf.h"
#include <stdint.h>

static uint64_t ring3_calls;
static uint64_t exit_calls;

int64_t syscall_dispatch(struct interrupt_frame *f) {
    if ((f->cs & 3) == 3) ring3_calls++;   // iret 帧里 CS 的 RPL 证明来自用户态
    switch (f->rax) {
        case SYS_WRITE: {
            uint64_t fd = f->rdi;
            const char *buf = (const char *)f->rsi;
            uint64_t len = f->rdx;
            if (fd != 1 || buf == NULL) return -1;   // 目前只有 fd=1 串口
            for (uint64_t i = 0; i < len; i++) serial_putc(buf[i]);
            return (int64_t)len;
        }
        case SYS_YIELD:
            sched_request();
            return 0;
        case SYS_EXIT: {
            exit_calls++;
            struct thread *t = sched_current();
            if (t != NULL) t->state = THREAD_DEAD;
            sched_request();
            return 0;
        }
        case SYS_GET_TICKS:
            return (int64_t)timer_ticks();
        case SYS_GETPID: {
            struct thread *t = sched_current();
            return t != NULL ? (int64_t)t->id : -1;
        }
        default:
            return -1;
    }
}

uint64_t do_syscall(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3) {
    uint64_t ret = num;
    __asm__ volatile ("int $0x80"
                      : "+a"(ret)
                      : "D"(a1), "S"(a2), "d"(a3)
                      : "memory");
    return ret;
}

void syscall_init(void) {
    kprintf("[B.9] syscall gate int 0x80 (vector 128, DPL=3), %d calls registered\n",
            SYS_MAX);
}

uint64_t syscall_ring3_count(void) { return ring3_calls; }
uint64_t syscall_exit_count(void)   { return exit_calls; }
