#ifndef DESOS_SYSCALL_H
#define DESOS_SYSCALL_H

#include <stdint.h>
#include "idt.h"

// B.9 系统调用：统一入口 int 0x80（IDT 向量 128，DPL=3 允许 ring3）。
// 约定：rax = 调用号，rdi/rsi/rdx = 参数 1/2/3，返回值写回栈帧的 rax。
// 调用号定义在 desos_abi.h（D.1 起，与用户态 libc 共享唯一出处）。
#define SYSCALL_VECTOR 0x80

#include "desos_abi.h"

void     syscall_init(void);
int64_t  syscall_dispatch(struct interrupt_frame *f);
uint64_t do_syscall(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3);
uint64_t syscall_ring3_count(void);   // 从 ring3 发起的调用次数（自检验证用户态）
uint64_t syscall_exit_count(void);    // SYS_EXIT 次数

#endif // DESOS_SYSCALL_H
