#ifndef DESOS_SHELL_H
#define DESOS_SHELL_H

#include <stdint.h>

// B.11 最小 Shell（内核态，允许「内核态或用户态都可」）
//   - 输入：COM1 轮询 + PS/2 键盘 IRQ1，统一进环形缓冲；
//   - 行编辑：可打印字符回显、退格、回车执行；C.1b 起支持光标移动 / Home / End / Delete；
//   - 内建命令：help / echo / mem / ticks / pid / threads / run / version / clear / halt。
// 输入与执行分离，便于自检用 shell_inject() 走完整的「输入 -> 回显 -> 解析执行」链路。

// C.1b：kbd.c 把 0xE0 扩展键翻译成这些私有控制码送进输入缓冲。
// 取 0x80 以上，避开 ASCII 可打印区与 \b / \r / \n。
#define KEY_UP      0x80
#define KEY_DOWN    0x81
#define KEY_LEFT    0x82
#define KEY_RIGHT   0x83
#define KEY_HOME    0x84
#define KEY_END     0x85
#define KEY_DELETE  0x86
#define KEY_INSERT  0x87
#define KEY_PGUP    0x88
#define KEY_PGDN    0x89

void     shell_init(void);
void     shell_run(void);                    // 交互主循环，不返回
// D.5：进入交互模式前重置行状态并打印首帧 prompt（自检也可调用验证）
void     shell_prepare_interactive(void);
int      shell_exec_line(const char *line); // 执行一行，返回 0 表示识别并执行
void     shell_input_push(char c);           // 串口/键盘/自检注入
void     shell_inject(const char *s);        // 注入一串字符（自检用）
void     shell_input_flush(void);            // 清空输入/行缓冲（自检用）
int      shell_process_input(void);          // 消费输入缓冲，返回执行行数
uint64_t shell_command_count(void);

// C.1b 自检用：只读行缓冲状态
int         shell_line_length(void);
int         shell_cursor_pos(void);
const char *shell_line_text(void);           // NUL 结尾

#endif // DESOS_SHELL_H
