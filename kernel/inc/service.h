#ifndef DESOS_SERVICE_H
#define DESOS_SERVICE_H

#include <stdint.h>

// D.3 服务管理：内核侧服务注册表（对应 ProjectRoad D.3）
//
// 每个系统服务一条记录：名字 / 归属权限级 / 运行状态。
// D.3a 先做注册与查询（内置服务 + Shell services + SYS_SVC_INFO）；
// D.3b 起 init 用户进程开机拉起服务，动态 start/stop 留待有 IPC 后扩展。

#define SVC_MAX       16
#define SVC_NAME_MAX  12

#define SVC_STOPPED   0
#define SVC_RUNNING   1

void service_init(void);
int  service_register(const char *name, uint8_t priv, int running);
int  service_count(void);

// 把第 idx 条服务拷贝成文本行 "name running kernel"（bufsize >= 32）。
// idx < 0 时返回总条数（供用户态 svc_count）。0=成功，-1=参数非法。
int  service_info(int idx, char *buf, int bufsize);

#endif // DESOS_SERVICE_H
