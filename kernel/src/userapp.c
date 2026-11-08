// userapp.c —— D.3b：按名字查内嵌用户程序清单
#include "userapp.h"
#include <stddef.h>   // NULL

extern const struct userapp_entry userapp_table[];
extern const uint64_t userapp_table_len;

static int name_eq(const char *a, const char *b) {
    if (a == NULL || b == NULL) return 0;
    while (*a != 0 && *a == *b) { a++; b++; }
    return *a == *b;
}

int userapp_count(void) { return (int)userapp_table_len; }

const struct userapp_entry *userapp_at(int idx) {
    if (idx < 0 || idx >= userapp_count()) return NULL;
    return &userapp_table[idx];
}

const struct userapp_entry *userapp_find(const char *name) {
    for (int i = 0; i < userapp_count(); i++) {
        if (name_eq(userapp_table[i].name, name)) return &userapp_table[i];
    }
    return NULL;
}