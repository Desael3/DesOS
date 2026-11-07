#ifndef DESOS_APIC_H
#define DESOS_APIC_H

#include <stdint.h>

// B.6b：ACPI MADT -> LAPIC / IOAPIC MMIO 映射 -> IOAPIC 重定向 -> 禁用 8259A PIC。
// 切换成功后 IRQ 的 EOI 改由 apic_send_eoi() 写 LAPIC EOI 寄存器（见 idt.c）；
// 若 APIC 中断验不通，apic_fallback_to_pic() 会恢复到 B.6a 的 PIC 路径。

void     apic_set_rsdp(uint64_t rsdp_addr, uint64_t hhdm_offset);
int      apic_init(void);                 // 0=成功，负数=失败原因
int      apic_enabled(void);
void     apic_send_eoi(void);
void     apic_fallback_to_pic(void);

uint64_t apic_lapic_phys(void);
uint64_t apic_ioapic_phys(void);
uint8_t  apic_bsp_id(void);
uint32_t apic_lapic_version(void);
uint32_t apic_ioapic_pins(void);
uint32_t apic_madt_flags(void);
uint32_t apic_override_count(void);
uint32_t apic_gsi_for_irq(uint8_t irq);   // MADT Interrupt Source Override 后的 GSI
uint32_t apic_redir_low(uint32_t pin);    // 读回 IOAPIC 重定向表低 32 位

#endif // DESOS_APIC_H
