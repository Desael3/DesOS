#ifndef DESOS_GDT_H
#define DESOS_GDT_H

#include <stdint.h>

// B.4 自有 GDT 与 TSS
//   - 替换 Limine 提供的临时 GDT（其 CS=0x28），建立 DesOS 自己的段布局；
//   - 平坦 64 位内核/用户代码数据段 + 64 位 TSS（rsp0 + IST1）；
//   - 用户态段/选择子先备好，供 B.9 系统调用与 B.10 用户态程序使用。
// 选择子布局（每项 8 字节；TSS 描述符占 0x28/0x30 两个槽）：
//   0x00 null | 0x08 kernel code | 0x10 kernel data
//   0x18 user data   | 0x20 user code | 0x28 TSS(low) | 0x30 TSS(high)
#define KERNEL_CODE_SEL 0x08
#define KERNEL_DATA_SEL 0x10
#define USER_DATA_SEL   0x18
#define USER_CODE_SEL   0x20
#define TSS_SEL         0x28

void     gdt_init(void);
void     gdt_set_kernel_stack(uint64_t rsp0);   // B.10：切换用户线程时更新 TSS.rsp0
uint64_t gdt_base(void);            // 当前 GDTR.base
uint16_t gdt_limit(void);           // 当前 GDTR.limit
uint16_t gdt_kernel_cs(void);       // 当前 CS
uint16_t gdt_task_register(void);   // 当前 TR
uint64_t gdt_tss_rsp0(void);
uint64_t gdt_tss_base(void);

#endif // DESOS_GDT_H
