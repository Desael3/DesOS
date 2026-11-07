#ifndef DESOS_B_SELFTEST_H
#define DESOS_B_SELFTEST_H

// B 阶段自检（B.1 PMM / B.2 VMM / B.3 heap / B.4 GDT-TSS / B.5 IDT+#PF /
//             B.6a PIC+PIT / B.6b APIC / B.7 sched / B.8 threads / B.9 syscall /
//             B.10 用户态 ELF+ring3 / B.11 最小 Shell）。
// 结果全部打到 COM1 串口，便于 QEMU -nographic 自动验收。
// 返回 0 表示全部通过；非 0 为失败项数量。
int b_selftest_run(void);

#endif // DESOS_B_SELFTEST_H
