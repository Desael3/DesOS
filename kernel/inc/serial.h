#ifndef DESOS_SERIAL_H
#define DESOS_SERIAL_H

#include <stddef.h>

// 串口日志（COM1，I/O 端口基址 0x3F8；对应 ProjectRoad A.8）
// 早期内核调试用：-serial stdio 即可在 QEMU 看到内核打印。

// 初始化 COM1（8N1、波特率 115200；BIOS/Limine 通常已设好，这里做一次显式初始化）。
void serial_init(void);

// 发送一个字节（未就绪时轮询 LSR 的 THR 空位）。
void serial_putc(char c);

// 发送以 NUL 结尾的字符串（'\n' 自动补 '\r'，符合终端习惯）。
void serial_puts(const char *s);

// 接收（B.11 Shell 输入用）：LSR bit0 = 接收数据就绪。
int  serial_available(void);
char serial_getc(void);

#endif // DESOS_SERIAL_H
