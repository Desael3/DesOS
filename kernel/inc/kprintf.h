#ifndef DESOS_KPRINTF_H
#define DESOS_KPRINTF_H

// 内核轻量格式化输出（走 COM1 串口；对应 ProjectRoad B 阶段基础设施）。
// 支持：%c %s %d %u %x %X %p %%，以及 l / ll 长度前缀、最小数字宽度（如 %016lx）。
// 不分配内存、不递归，可在中断/异常上下文早期使用。
void kprintf(const char *fmt, ...);

#endif // DESOS_KPRINTF_H
