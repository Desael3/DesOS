#ifndef DESOS_KBD_H
#define DESOS_KBD_H

#include <stdint.h>

// C.1 PS/2 键盘（set 1，IRQ1）。
// B.11 只做了主键区小写映射；C.1a 补上修饰键 / 0xE0 扩展前缀 / 字符映射，
// C.1b 把方向键 / Home / End / Delete 等扩展键送进 Shell 行编辑。
// PS/2 鼠标（IRQ12）留 C.1c。
// 自检通过 kbd_inject_scancode() 走与 IRQ1 完全相同的链路。

#define KBD_MOD_LSHIFT  (1u << 0)
#define KBD_MOD_RSHIFT  (1u << 1)
#define KBD_MOD_LCTRL   (1u << 2)
#define KBD_MOD_RCTRL   (1u << 3)
#define KBD_MOD_LALT    (1u << 4)
#define KBD_MOD_RALT    (1u << 5)
#define KBD_MOD_CAPS    (1u << 6)
#define KBD_MOD_NUMLOCK (1u << 7)
#define KBD_MOD_SCROLL  (1u << 8)

#define KBD_MOD_SHIFT   (KBD_MOD_LSHIFT | KBD_MOD_RSHIFT)
#define KBD_MOD_CTRL    (KBD_MOD_LCTRL  | KBD_MOD_RCTRL)
#define KBD_MOD_ALT     (KBD_MOD_LALT   | KBD_MOD_RALT)

void     kbd_init(void);                     // 打开 IRQ1 并复位状态
void     kbd_on_irq1(void);                  // 由 IDT 的 IRQ1 分发调用
void     kbd_inject_scancode(uint8_t sc);   // IRQ1 与自检共用的 scancode 链路
void     kbd_reset(void);                   // 自检用：清空状态与统计

uint32_t kbd_modifiers(void);               // 当前修饰键位掩码
char     kbd_last_char(void);               // 最近一次提交的字符
uint16_t kbd_last_key(void);                // 最近键码，E0 扩展键编码为 0xE0xx
uint32_t kbd_key_events(void);              // 提交字符的事件数
uint32_t kbd_control_events(void);          // 提交扩展键控制码的事件数

#endif // DESOS_KBD_H
