#ifndef DESOS_IDT_H
#define DESOS_IDT_H

#include <stdint.h>

// B.5 IDT / 异常向量 / 缺页处理
//
// 栈帧布局必须与 kernel/asm/idt_stubs.asm 中 isr_common 的 push 顺序一致：
//   低地址 -> r15..r8, rbp, rdi, rsi, rdx, rcx, rbx, rax,
//             vector, error_code, rip, cs, rflags, rsp, ss
struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed));

void     idt_init(void);
uint64_t idt_base(void);
uint16_t idt_limit(void);
uint64_t idt_breakpoint_count(void);
uint64_t idt_pagefault_count(void);
uint64_t idt_irq_count(uint8_t irq);

// 允许指定虚拟地址发生「一次」缺页：#PF 处理里为它分配物理页并映射，
// 返回后 CPU 重试原指令。用于验证缺页处理链路；也是后续按需分页的雏形。
void idt_arm_demand_fault(uint64_t va);

#endif // DESOS_IDT_H
