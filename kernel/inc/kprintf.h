#ifndef DESOS_KPRINTF_H
#define DESOS_KPRINTF_H

// 内核轻量格式化输出（走 COM1 串口；对应 ProjectRoad B 阶段基础设施）。
// 支持：%c %s %d %u %x %X %p %%，以及 l / ll 长度前缀、最小数字宽度（如 %016lx）。
// 不分配内存、不递归，可在中断/异常上下文早期使用。
void kprintf(const char *fmt, ...);

// D.5 屏幕镜像开关：quiet=1 时 kprintf 只写 COM1 串口（日志完整保留），
// 不再把自检/命令演示刷到帧缓冲屏幕。默认 0（保持旧行为）。
void kprintf_set_quiet(int quiet);
int  kprintf_is_quiet(void);

#endif // DESOS_KPRINTF_H
