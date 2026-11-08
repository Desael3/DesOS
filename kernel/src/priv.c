// priv.c —— D.2 权限分级：capability 校验矩阵
#include "priv.h"
#include "kstring.h"

// kstring.h 只有内存函数；本模块用到的字符串比较就地实现
static int kstreq(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) { a++; b++; }
    return *a == *b;
}

// 每个能力允许的最低权限级（数值 <= min_level 放行）
static const struct cap_rule {
    uint32_t cap;
    uint8_t  min_level;
} cap_rules[] = {
    { CAP_POWER,       PRIV_SUDO   },   // sudo-user 经控制台授权可关机/重启
    { CAP_THREAD_CTRL, PRIV_SYSTEM },   // 仅内核与系统服务可管理其它线程
    { CAP_HW_ACCESS,   PRIV_SYSTEM },   // 仅内核与系统服务可直接碰硬件
};

#define CAP_RULES (sizeof(cap_rules) / sizeof(cap_rules[0]))

int priv_check_cap(uint8_t priv, uint32_t cap) {
    if (priv > PRIV_USER) return -1;                 // 非法权限级
    for (uint32_t i = 0; i < CAP_RULES; i++) {
        if (cap_rules[i].cap == cap)
            return (priv <= cap_rules[i].min_level) ? 0 : -1;
    }
    return -1;                                       // 未定义的 capability 一律拒绝
}

const char *priv_name(uint8_t priv) {
    static const char *names[4] = { "kernel", "system", "sudo", "user" };
    return (priv <= PRIV_USER) ? names[priv] : "?";
}

int priv_parse(const char *s, uint8_t *out) {
    static const char *names[4] = { "kernel", "system", "sudo", "user" };
    if (s == NULL || out == NULL) return -1;
    for (uint8_t i = 0; i <= PRIV_USER; i++) {
        if (kstreq(s, names[i])) { *out = i; return 0; }
    }
    return -1;
}
