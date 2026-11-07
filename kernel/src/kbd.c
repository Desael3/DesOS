// kbd.c —— B.11 PS/2 键盘最小驱动（对应 C.1 的最小子集）

#include "kbd.h"
#include "shell.h"
#include "pic.h"
#include <stdint.h>

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

// PS/2 set 1 scancode -> ASCII（只覆盖主键区，0 表示忽略）
static const char scancode_map[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '-', [0x0D] = '=', [0x0E] = '\b',
    [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']', [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x39] = ' ',
};

void kbd_init(void) {
    pic_clear_mask(1);   // IRQ1
}

// IRQ1 handler 与自检的软件注入共用同一条 scancode -> Shell 输入链路。
void kbd_inject_scancode(uint8_t sc) {
    if (sc & 0x80) return;          // 断码（松开按键）
    if (sc >= 128) return;
    char c = scancode_map[sc];
    if (c == '\n') {
        shell_input_push('\r');
    } else if (c != 0) {
        shell_input_push(c);
    }
}

void kbd_on_irq1(void) {
    uint8_t sc = inb(0x60);        // 读数据端口即确认中断
    kbd_inject_scancode(sc);
}
