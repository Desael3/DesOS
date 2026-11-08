#ifndef DESOS_USERAPP_H
#define DESOS_USERAPP_H

#include <stdint.h>

// D.3b：内嵌用户程序清单条目（由 tools/build_user.py 生成 NASM 表）
// 表项布局必须与 user_image.asm 的 3 个 dq 顺序一致：name/start/end。
struct userapp_entry {
    const char    *name;
    const uint8_t *start;
    const uint8_t *end;
};

int userapp_count(void);
const struct userapp_entry *userapp_at(int idx);
const struct userapp_entry *userapp_find(const char *name);

#endif // DESOS_USERAPP_H