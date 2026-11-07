// pic.c —— B.6 8259A PIC 初始化 + PIT 中断源（对应 ProjectRoad B.6 前半）

#include "pic.h"
#include "kprintf.h"
#include <stdint.h>

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

#define PIT_CH0   0x40
#define PIT_CMD   0x43
#define PIT_BASE_HZ 1193182U

static uint64_t pit_ticks;

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

void pic_init(void) {
    // ICW1：边沿触发、级联、需要 ICW4
    outb(PIC1_CMD, 0x11);  io_wait();
    outb(PIC2_CMD, 0x11);  io_wait();
    // ICW2：主片 IRQ0-7 -> 0x20..0x27，从片 IRQ8-15 -> 0x28..0x2F
    outb(PIC1_DATA, 0x20); io_wait();
    outb(PIC2_DATA, 0x28); io_wait();
    // ICW3：主片 IRQ2 接从片；从片级联标识 2
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();
    // ICW4：8086 模式、普通 EOI、非缓冲
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();
    // 默认全屏蔽，按需打开
    outb(PIC1_DATA, 0xFF); io_wait();
    outb(PIC2_DATA, 0xFF); io_wait();

    kprintf("[B.6] PIC remapped IRQ0-15 -> vectors 32-47, all IRQs masked\n");
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

void pic_set_mask(uint8_t irq) {
    if (irq > 15) return;
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = (uint8_t)(1u << (irq & 7));
    outb(port, (uint8_t)(inb(port) | bit));
}

void pic_clear_mask(uint8_t irq) {
    if (irq > 15) return;
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = (uint8_t)(1u << (irq & 7));
    outb(port, (uint8_t)(inb(port) & (uint8_t)~bit));
}

uint8_t pic_imr_master(void) { return inb(PIC1_DATA); }
uint8_t pic_imr_slave(void)  { return inb(PIC2_DATA); }

void pit_init(uint32_t hz) {
    if (hz == 0) hz = 1000;
    uint32_t divisor = PIT_BASE_HZ / hz;
    if (divisor > 0xFFFF) divisor = 0xFFFF;
    pit_ticks = 0;
    outb(PIT_CMD, 0x36);                        // ch0, 先低后高字节, mode 3
    outb(PIT_CH0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0, (uint8_t)((divisor >> 8) & 0xFF));
    kprintf("[B.6] PIT ch0 %lu Hz (divisor=%lu)\n", (uint64_t)hz, (uint64_t)divisor);
}

uint64_t pit_tick_count(void) { return pit_ticks; }

void pit_on_irq(uint8_t irq) {
    if (irq == 0) pit_ticks++;
}
