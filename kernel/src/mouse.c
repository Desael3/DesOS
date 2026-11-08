// mouse.c —— C.1c PS/2 鼠标（IRQ12，标准 3 字节包）

#include "mouse.h"
#include "apic.h"
#include "pic.h"
#include "kprintf.h"
#include <stdint.h>

#define PS2_STATUS 0x64
#define PS2_DATA   0x60

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline void io_wait(void) { outb(0x80, 0); }

static volatile int32_t  x, y;
static volatile uint8_t  buttons;
static volatile uint32_t packets;
static volatile uint32_t irqs;
static uint8_t pkt[3];
static int    phase;

static void ps2_wait_input(void) {          // 等 8042 输入缓冲空（status bit1 = 0）
    uint64_t guard = 0;
    while ((inb(PS2_STATUS) & 0x02) && guard < 100000) { io_wait(); guard++; }
}

static void ps2_wait_output(void) {         // 等 8042 输出缓冲满（status bit0 = 1）
    uint64_t guard = 0;
    while (!(inb(PS2_STATUS) & 0x01) && guard < 100000) { io_wait(); guard++; }
}

// 向第二 PS/2 端口写命令：先发 0xD4，再发命令字节。
// 不等 ACK：ACK(0xFA) 会由 IRQ12 读走，包对齐检查会把它丢掉。
static void mouse_write(uint8_t cmd) {
    ps2_wait_input();
    outb(PS2_STATUS, 0xD4);
    ps2_wait_input();
    outb(PS2_DATA, cmd);
}

void mouse_reset(void) {
    x = y = 0;
    buttons = 0;
    packets = 0;
    irqs = 0;
    phase = 0;
    pkt[0] = pkt[1] = pkt[2] = 0;
}

void mouse_init(void) {
    mouse_reset();

    ps2_wait_input();
    outb(PS2_STATUS, 0xA8);                 // 启用第二 PS/2 端口

    ps2_wait_input();
    outb(PS2_STATUS, 0x20);                 // Read Configuration Byte
    ps2_wait_output();
    uint8_t cfg = inb(PS2_DATA);
    cfg |= 0x02;                            // bit1：第二端口 IRQ12 使能
    cfg &= (uint8_t)~0x20;                  // bit5：清 Mouse Clock Disable
    ps2_wait_input();
    outb(PS2_STATUS, 0x60);                 // Write Configuration Byte
    ps2_wait_input();
    outb(PS2_DATA, cfg);

    mouse_write(0xF6);                      // Set Defaults
    mouse_write(0xF4);                      // Enable Data Reporting

    pic_clear_mask(12);                     // PIC 回退路径也要放开 IRQ12
    kprintf("[C.1] PS/2 mouse enabled (IRQ12 -> vector 44, cfg=0x%x)\n", (unsigned)cfg);
}

// 标准 3 字节包：首字节 bit3 恒为 1，bit6/bit7 为溢出标志（正常为 0）。
// 用 (b & 0xC8) == 0x08 做对齐，可把 ACK(0xFA) 这类字节挡在包外。
void mouse_inject_byte(uint8_t b) {
    if (phase == 0) {
        if ((b & 0xC8) != 0x08) return;
        pkt[0] = b;
        phase = 1;
        return;
    }
    pkt[phase++] = b;
    if (phase < 3) return;
    phase = 0;

    int32_t dx = (int32_t)pkt[1];
    int32_t dy = (int32_t)pkt[2];
    if (pkt[0] & 0x10) dx -= 256;           // X 符号位（9 位补码）
    if (pkt[0] & 0x20) dy -= 256;           // Y 符号位
    if ((pkt[0] & 0x40) == 0) x += dx;      // X overflow 时忽略本次位移
    if ((pkt[0] & 0x80) == 0) y += dy;
    buttons = pkt[0] & 0x07;
    packets++;
}

void mouse_on_irq12(void) {
    uint8_t b = inb(PS2_DATA);              // 读数据端口即确认中断
    irqs++;
    mouse_inject_byte(b);
}

// USB HID 鼠标相对位移喂入（与 PS/2 共享 x/y/buttons 状态）。
void mouse_inject_rel(int32_t dx, int32_t dy, uint8_t btns) {
    x += dx;
    y += dy;
    buttons = btns & 0x07;
    packets++;
}

int32_t  mouse_x(void)             { return x; }
int32_t  mouse_y(void)             { return y; }
uint8_t  mouse_buttons(void)       { return buttons; }
uint32_t mouse_packet_count(void)  { return packets; }
uint32_t mouse_irq_count(void)     { return irqs; }
