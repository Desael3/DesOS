#ifndef DESOS_PIC_H
#define DESOS_PIC_H

#include <stdint.h>

// B.6 中断控制器（先行部分：8259A PIC）
//   - 重映射 IRQ0-15 到 IDT 向量 32-47（避开异常向量 0-31）；
//   - 提供 EOI、IRQ 掩码开关、IMR 读取；
//   - PIT 通道 0 仅作为「验证 PIC 中断链路」的中断源，
//     正式的定时器抽象 / 调度器留给 B.7。
// B.6b 已实现 APIC 切换（见 apic.c / apic.h）：MADT -> LAPIC/IOAPIC MMIO ->
// IOAPIC 重定向 -> 屏蔽并旁路 PIC -> LAPIC EOI。切换成功后 IRQ 的 EOI 由
// apic_send_eoi() 负责；PIC 路径保留用于早期初始化与 APIC 验证失败时回退。

void     pic_init(void);
void     pic_send_eoi(uint8_t irq);
void     pic_set_mask(uint8_t irq);
void     pic_clear_mask(uint8_t irq);
uint8_t  pic_imr_master(void);
uint8_t  pic_imr_slave(void);

void     pit_init(uint32_t hz);
uint64_t pit_tick_count(void);
void     pit_on_irq(uint8_t irq);   // 由 IDT 的 IRQ 分发调用

#endif // DESOS_PIC_H
