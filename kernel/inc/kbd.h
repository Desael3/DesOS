#ifndef DESOS_KBD_H
#define DESOS_KBD_H

#include <stdint.h>

// B.11 输入通道之二：PS/2 键盘（IRQ1）。
// 这是 C.1 的最小子集，只为 Shell 提供按键输入：US 布局、无 shift/ctrl 修饰、
// 忽略断码与扩展键。串口 COM1 输入的轮询在 shell.c 里。
void kbd_init(void);                     // 打开 IRQ1
void kbd_on_irq1(void);                   // 由 IDT 的 IRQ1 分发调用
void kbd_inject_scancode(uint8_t sc);    // 自检用：与 IRQ1 共用 scancode 映射链路

#endif // DESOS_KBD_H
