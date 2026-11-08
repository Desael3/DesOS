// service.c —— D.3 服务管理：内核侧服务注册表
#include "service.h"
#include "priv.h"
#include "kprintf.h"
#include <stddef.h>   // NULL

struct svc {
    char    name[SVC_NAME_MAX];
    uint8_t priv;
    uint8_t state;      // SVC_RUNNING / SVC_STOPPED
};

static struct svc table[SVC_MAX];
static int used;

int service_register(const char *name, uint8_t priv, int running) {
    if (name == NULL || priv > PRIV_USER || used >= SVC_MAX) return -1;
    struct svc *s = &table[used];
    int i = 0;
    for (; i < SVC_NAME_MAX - 1 && name[i] != 0; i++) s->name[i] = name[i];
    s->name[i] = 0;
    s->priv = priv;
    s->state = running ? SVC_RUNNING : SVC_STOPPED;
    used++;
    return 0;
}

int service_count(void) { return used; }

int service_info(int idx, char *buf, int bufsize) {
    if (idx < 0) return used;                       // 供用户态 svc_count 查询
    if (idx >= used || buf == NULL || bufsize < 32) return -1;
    const struct svc *s = &table[idx];
    int p = 0;
    for (int i = 0; i < SVC_NAME_MAX && s->name[i] != 0; i++) buf[p++] = s->name[i];
    buf[p++] = ' ';
    const char *st = (s->state == SVC_RUNNING) ? "running" : "stopped";
    while (*st != 0) buf[p++] = *st++;
    buf[p++] = ' ';
    const char *pn = priv_name(s->priv);
    while (*pn != 0) buf[p++] = *pn++;
    buf[p] = 0;
    return 0;
}

void service_init(void) {
    // 内置服务（D.3a）：C 阶段子系统直接注册。fb/kbd/mouse/acpi 在两类测试
    // 环境（QEMU/VMware）都必然初始化成功；ata/net/usb 需要对应硬件，
    // 当前测试环境无 IDE 盘 / e1000 / xHCI，注册为 stopped（D.3b 起由
    // 各驱动按真实探测结果更新状态，init 用户进程按此拉起服务）。
    service_register("fb",   PRIV_KERNEL, 1);
    service_register("kbd",  PRIV_KERNEL, 1);
    service_register("mouse", PRIV_KERNEL, 1);
    service_register("acpi", PRIV_KERNEL, 1);
    service_register("ata",  PRIV_SYSTEM, 0);
    service_register("net",  PRIV_SYSTEM, 0);
    service_register("usb",  PRIV_SYSTEM, 0);
    kprintf("[D.3] service registry: %d builtin services (4 running, 3 stopped)\n", used);
}
