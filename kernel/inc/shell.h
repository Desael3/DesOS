#ifndef DESOS_SHELL_H
#define DESOS_SHELL_H

#include <stdint.h>

// B.11 最小 Shell（内核态，允许「内核态或用户态都可」）
//   - 输入：COM1 轮询 + PS/2 键盘 IRQ1，统一进环形缓冲；
//   - 行编辑：可打印字符回显、退格、回车执行；
//   - 内建命令：help / echo / mem / ticks / pid / threads / run / version / clear / halt。
// 输入与执行分离，便于自检用 shell_inject() 走完整的「输入 -> 回显 -> 解析执行」链路。

void     shell_init(void);
void     shell_run(void);                    // 交互主循环，不返回
int      shell_exec_line(const char *line); // 执行一行，返回 0 表示识别并执行
void     shell_input_push(char c);           // 串口/键盘/自检注入
void     shell_inject(const char *s);        // 注入一串字符（自检用）
int      shell_process_input(void);          // 消费输入缓冲，返回执行行数
uint64_t shell_command_count(void);

#endif // DESOS_SHELL_H
