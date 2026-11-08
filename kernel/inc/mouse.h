#ifndef DESOS_MOUSE_H
#define DESOS_MOUSE_H

#include <stdint.h>

// C.1c PS/2 鼠标（IRQ12，标准 3 字节包）。
// IRQ12 handler 与自检共走 mouse_inject_byte() 这一条包解析链路。
// IOAPIC 的 IRQ12 -> 向量 44 路由在 apic_init() 里配置；PIC 模式由
// mouse_init() 调 pic_clear_mask(12) 放行。

void     mouse_init(void);                  // 启用第二端口 + 放开 IRQ12 + 数据流
void     mouse_on_irq12(void);              // 由 IDT 的 IRQ12 分发调用
void     mouse_inject_byte(uint8_t b);      // IRQ12 与自检共用
void     mouse_inject_rel(int32_t dx, int32_t dy, uint8_t buttons); // USB HID 相对位移
void     mouse_reset(void);                 // 自检用：状态与统计归零

int32_t  mouse_x(void);
int32_t  mouse_y(void);
uint8_t  mouse_buttons(void);               // bit0 左 / bit1 右 / bit2 中
uint32_t mouse_packet_count(void);
uint32_t mouse_irq_count(void);

#endif // DESOS_MOUSE_H
