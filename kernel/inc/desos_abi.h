#ifndef DESOS_ABI_H
#define DESOS_ABI_H

// DesOS 用户态 ABI —— 内核（kernel/inc/syscall.h）与用户态 libc
// （user/libc/desos.h，经 -I kernel/inc 引入）共享的唯一出处。
//
// int 0x80 约定：rax = 调用号，rdi/rsi/rdx = 参数 1/2/3，返回值写回 rax。

#define SYS_WRITE     0   // (fd=1, const char *buf, uint64_t len) -> len
#define SYS_YIELD     1   // () -> 0
#define SYS_EXIT      2   // () -> 0，标记当前线程 DEAD 并让出
#define SYS_GET_TICKS 3   // () -> timer_ticks()
#define SYS_GETPID    4   // () -> 当前线程 id
#define SYS_PRIV_INFO 5   // () -> 当前线程权限级 (0=kernel..3=user)，负数=错误
#define SYS_POWER_OP  6   // (req: 0=poweroff 1=reboot 2=仅校验) -> 0=允许/已执行，-1=拒绝(CAP_POWER)
#define SYS_SVC_INFO  7   // (idx, char *buf, len>=32) -> 0=成功写入服务行 / 总数(idx<0)，-1=拒绝
#define SYS_MAX       8

#define DESOS_STDOUT  1   // 目前唯一支持的标准输出（内核串口）

#endif // DESOS_ABI_H
