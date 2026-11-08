// hello.c —— DesOS 用户态演示程序（D.1 libc + D.2 权限演示）
//
// 由 tools/build_user.py 与 user/libc/*.c 一起交叉编译成静态 ELF64，
// 镜像随内核一起打进 ISO。第一行输出保持与 B.10 验收模式串一致：
// "Hello from DesOS userland (ring3)!"
#include "desos.h"

void _start(void) {
    printf("Hello from DesOS userland (ring3)!\n");

    // libc 能力演示：格式化输出 + syscall 封装 + string 子集
    const char *word = "DesOS-libc";
    printf("libc demo: pid=%ld ticks=%lu strlen(%s)=%u strcmp(abc,abd)=%d\n",
           getpid(), sys_ticks(), word,
           (unsigned)strlen(word), strcmp("abc", "abd"));

    // D.2 权限演示：读当前权限级；尝试受 CAP_POWER 保护的操作（仅校验，不真关机）
    long priv = desos_syscall(SYS_PRIV_INFO, 0, 0, 0);
    long pw   = desos_syscall(SYS_POWER_OP, 2, 0, 0);   // req=2: check-only
    printf("priv level=%ld power-op: %s\n", priv,
           (pw == 0) ? "allowed" : "denied");

    // D.3 服务表：用户态经 SYS_SVC_INFO 读内核服务注册表
    long nsvc = svc_count();
    printf("services: %ld\n", nsvc);
    for (long i = 0; i < nsvc && i < 16; i++) {
        char row[32];
        if (svc_info(i, row, sizeof(row)) == 0) printf("  %s\n", row);
    }

    exit(0);
}
