#ifndef DESOS_PRIV_H
#define DESOS_PRIV_H

#include <stdint.h>

// D.2 权限分级（对应 ProjectRoad D.2，目标 7）
//
// 四级权限模型（数值越小权限越高）：
//   kernel-user  ring0 内核自身，最高权限
//   system-user  系统服务/守护进程，受限特权，管硬件与系统资源
//   sudo-user    管理员，经控制台授权提权（Shell 输入即授权）
//   user         普通用户，最小权限
//
// 校验模型：capability 矩阵。每个能力定义「允许的最低权限级」，
// priv <= min_level 即放行。VFS / 进程管理层落地后按同一套 capability
// 做 ACL 校验（D.2 后续小步），当前先覆盖系统调用层。

typedef enum {
    PRIV_KERNEL = 0,   // 内核自身（ring0），最高权限
    PRIV_SYSTEM = 1,   // 系统服务/守护进程：受限特权，管硬件与系统资源
    PRIV_SUDO   = 2,   // 管理员：经控制台授权提权
    PRIV_USER   = 3,   // 普通用户：最小权限
} priv_level_t;

typedef enum {
    CAP_POWER       = 1u << 0,   // 关机 / 重启（SYS_POWER_OP）
    CAP_THREAD_CTRL = 1u << 1,   // 管理其它线程/进程（D.3 起使用）
    CAP_HW_ACCESS   = 1u << 2,   // 直接硬件/设备访问（VFS 落地后使用）
} priv_cap_t;

// capability 校验：0=允许，-1=拒绝（未知 capability 一律拒绝）
int priv_check_cap(uint8_t priv, uint32_t cap);

const char *priv_name(uint8_t priv);               // "kernel"/"system"/"sudo"/"user"

// 解析权限名（"kernel"/"system"/"sudo"/"user"），0=成功写入 *out
int priv_parse(const char *s, uint8_t *out);

#endif // DESOS_PRIV_H
