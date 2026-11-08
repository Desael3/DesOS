// idt.c —— B.5 IDT / 异常向量 / 缺页处理（对应 ProjectRoad B.5）
//
// 桩在 kernel/asm/idt_stubs.asm；本文件负责建表、分发、诊断与按需映射。

#include "idt.h"
#include "gdt.h"
#include "pic.h"
#include "pmm.h"
#include "vmm.h"
#include "kstring.h"
#include "kprintf.h"
#include "timer.h"
#include "syscall.h"
#include "kbd.h"
#include "mouse.h"
#include "apic.h"
#include <stdint.h>

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idtr_value {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

#define IDT_TYPE_INT_GATE 0x8E   // P=1, DPL=0, 64-bit interrupt gate

static struct idt_entry idt[256] __attribute__((aligned(16)));

extern uint64_t isr_stub_table[48];
extern void isr_stub_128(void);
extern void idt_flush(const struct idtr_value *idtr);

static uint64_t breakpoint_count;
static uint64_t pagefault_count;
static uint64_t irq_count[16];
static uint64_t demand_fault_va;   // 0 = 未武装

static const char *const exception_names[32] = {
    "Divide Error", "Debug", "NMI", "Breakpoint", "Overflow", "Bound Range",
    "Invalid Opcode", "Device Not Available", "Double Fault", "Coprocessor Overrun",
    "Invalid TSS", "Segment Not Present", "Stack-Segment Fault", "General Protection",
    "Page Fault", "Reserved", "x87 FPU Error", "Alignment Check", "Machine Check",
    "SIMD FP Exception", "Virtualization Exception", "Control Protection", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved"
};

static inline uint64_t read_cr2(void) {
    uint64_t v;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(v));
    return v;
}

static inline void read_idtr(struct idtr_value *out) {
    __asm__ volatile ("sidt %0" : "=m"(*out) : : "memory");
}

static void panic_halt(void) {
    kprintf("[B.5] PANIC: system halted\n");
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

void idt_init(void) {
    kmemset(idt, 0, sizeof(idt));
    for (uint32_t i = 0; i < 48; i++) {
        uint64_t addr = isr_stub_table[i];
        idt[i].offset_low  = (uint16_t)(addr & 0xFFFF);
        idt[i].selector    = KERNEL_CODE_SEL;
        idt[i].ist         = (i == 8 || i == 14) ? 1 : 0;  // #DF / #PF 用 TSS 的 IST1
        idt[i].type_attr   = IDT_TYPE_INT_GATE;
        idt[i].offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
        idt[i].offset_high = (uint32_t)(addr >> 32);
        idt[i].reserved    = 0;
    }

    // B.9 系统调用门：向量 0x80，DPL=3（0xEE），允许将来 ring3 调用。
    uint64_t sc = (uint64_t)&isr_stub_128;
    idt[SYSCALL_VECTOR].offset_low  = (uint16_t)(sc & 0xFFFF);
    idt[SYSCALL_VECTOR].selector    = KERNEL_CODE_SEL;
    idt[SYSCALL_VECTOR].ist         = 0;
    idt[SYSCALL_VECTOR].type_attr   = 0xEE;
    idt[SYSCALL_VECTOR].offset_mid  = (uint16_t)((sc >> 16) & 0xFFFF);
    idt[SYSCALL_VECTOR].offset_high = (uint32_t)(sc >> 32);

    struct idtr_value r = { (uint16_t)(sizeof(idt) - 1), (uint64_t)&idt };
    idt_flush(&r);
    kprintf("[B.5] IDT base=0x%016lx limit=%u, 48 IRQ/exception stubs + int 0x80 gate\n",
            idt_base(), (unsigned)idt_limit());
}

void idt_arm_demand_fault(uint64_t va) {
    demand_fault_va = va;
}

// 由 asm 的 isr_common 调用。返回即 iretq 回到被中断的指令流。
void isr_common_handler(struct interrupt_frame *f) {
    uint64_t v = f->vector;

    if (v == 14) {   // #PF
        uint64_t cr2 = read_cr2();
        pagefault_count++;
        uint64_t page = cr2 & ~(PAGE_SIZE - 1);
        if (demand_fault_va != 0 && page == (demand_fault_va & ~(PAGE_SIZE - 1))) {
            uint64_t phys = pmm_alloc_frame();
            if (phys != 0 && vmm_map(page, phys, VMM_PRESENT | VMM_WRITE) == 0) {
                demand_fault_va = 0;
                kprintf("[B.5] #PF handled: cr2=0x%016lx err=0x%lx rip=0x%016lx -> demand-mapped\n",
                        cr2, f->error_code, f->rip);
                return;
            }
        }
        kprintf("[B.5] FATAL #PF: cr2=0x%016lx err=0x%lx (P=%lu W/R=%lu U/S=%lu) rip=0x%016lx\n",
                cr2, f->error_code, (f->error_code & 1) ? 1UL : 0UL,
                (f->error_code & 2) ? 1UL : 0UL, (f->error_code & 4) ? 1UL : 0UL, f->rip);
        panic_halt();
    }

    if (v < 32) {
        if (v == 3) {   // #BP：自检用，打印后返回继续往下执行
            breakpoint_count++;
            kprintf("[B.5] #BP handled: rip=0x%016lx (returning)\n", f->rip);
            return;
        }
        kprintf("[B.5] FATAL exception vec=%lu (%s) err=0x%lx rip=0x%016lx cs=0x%lx rflags=0x%016lx\n",
                v, exception_names[v], f->error_code, f->rip, f->cs, f->rflags);
        panic_halt();
    }

    if (v == SYSCALL_VECTOR) {
        f->rax = (uint64_t)syscall_dispatch(f);
        return;
    }

    if (v >= 32 && v < 48) {
        uint8_t irq = (uint8_t)(v - 32);
        irq_count[irq]++;
        if (irq == 0) timer_on_tick();     // B.7：时间片到期请求重新调度
        if (irq == 1) kbd_on_irq1();      // B.11：PS/2 键盘输入
        if (irq == 12) mouse_on_irq12();  // C.1c：PS/2 鼠标（向量 44）
        pit_on_irq(irq);
        // B.6a 走 8259A EOI；B.6b 切换后必须写 LAPIC EOI，否则同一向量不再投递。
        if (apic_enabled()) apic_send_eoi(); else pic_send_eoi(irq);
        return;
    }

    kprintf("[B.5] FATAL unhandled vector=%lu rip=0x%016lx\n", v, f->rip);
    panic_halt();
}

uint64_t idt_base(void) {
    struct idtr_value v;
    read_idtr(&v);
    return v.base;
}

uint16_t idt_limit(void) {
    struct idtr_value v;
    read_idtr(&v);
    return v.limit;
}

uint64_t idt_breakpoint_count(void) { return breakpoint_count; }
uint64_t idt_pagefault_count(void)  { return pagefault_count; }
uint64_t idt_irq_count(uint8_t irq) { return irq < 16 ? irq_count[irq] : 0; }
