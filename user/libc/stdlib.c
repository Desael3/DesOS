// stdlib.c —— DesOS libc <stdlib.h> 子集（D.1，freestanding 自研）
#include "desos.h"

int atoi(const char *s) {
    int r = 0;
    int neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        r = r * 10 + (*s - '0');
        s++;
    }
    return neg ? -r : r;
}

int abs(int v) {
    return v < 0 ? -v : v;
}
