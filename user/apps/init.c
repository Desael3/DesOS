// init.c - D.3b：DesOS 第一个用户态 init 进程（由内核以 PRIV_SYSTEM 开机拉起）
//
// 职责（当前阶段骨架）：登录用户态、经 SYS_SVC_INFO 读内核服务注册表，
// 汇报各服务状态并标注哪些由内核拉起、哪些因无硬件暂不启动。
// 真正的动态 start/stop 等 IPC/服务控制面（SYS_SVC_CTRL）落地后补上。
#include "desos.h"

void _start(void) {
    long priv = desos_syscall(SYS_PRIV_INFO, 0, 0, 0);
    printf("init: DesOS userspace started (pid=%ld priv=%ld)\n", getpid(), priv);

    long n = svc_count();
    printf("init: %ld services registered\n", n);
    for (long i = 0; i < n && i < 16; i++) {
        char row[32];
        if (svc_info(i, row, sizeof(row)) != 0) continue;
        if (strncmp(row, "ata", 3) == 0 || strncmp(row, "net", 3) == 0 ||
            strncmp(row, "usb", 3) == 0) {
            printf("init: service %s (no hardware, skip)\n", row);
        } else {
            printf("init: service %s (managed by kernel)\n", row);
        }
    }
    printf("init: done\n");
    exit(0);
}
