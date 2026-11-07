// serial.c —— DesOS 极简串口日志（COM1，0x3F8；对应 ProjectRoad A.8）
//
// 仅做轮询输出，不依赖中断，适合内核早期（甚至中断还没开）打印调试信息。
// QEMU 启动加 -serial stdio 即可在宿主机看到输出。

#include "serial.h"
#include <stdint.h>

#define COM1 0x3F8

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

void serial_init(void) {
    // 设为 115200 8N1、DLL 可写（DLAB=1）
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);   // 除数锁存低位 -> 115200
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   // 8 位数据、无校验、1 停止位，DLAB=0
    outb(COM1 + 2, 0xC7);   // FIFO 使能 + 清 FIFO + 14 字节阈值
    outb(COM1 + 1, 0x00);   // 关闭串口中断（轮询模式）
}

void serial_putc(char c) {
    // LSR 在 COM1+5，bit5 = 发送保持寄存器空（THR empty）
    while ((inb(COM1 + 5) & 0x20) == 0) { /* 等待可发送 */ }
    outb(COM1, (uint8_t)c);
}

void serial_puts(const char *s) {
    for (; *s; s++) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s);
    }
}

int serial_available(void) {
    return (inb(COM1 + 5) & 0x01) != 0;
}

char serial_getc(void) {
    while (!serial_available()) { /* 等待 COM1 收到一个字节 */ }
    return (char)inb(COM1);
}
