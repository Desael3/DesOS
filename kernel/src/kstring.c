// kstring.c —— 内核内存操作（对应 ProjectRoad B 阶段基础设施）
// -ffreestanding 下没有 libc；build_kernel.py 另加 -fno-builtin，
// 但仍显式提供 memset/memcpy/memmove/memcmp，避免链接期出现未定义符号。

#include "kstring.h"
#include <stdint.h>

void *kmemset(void *dst, int c, size_t n) {
    volatile uint8_t *d = (volatile uint8_t *)dst;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)c;
    return dst;
}

void *kmemcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void *kmemmove(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d == s || n == 0) return dst;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}

int kmemcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}

void *memset(void *dst, int c, size_t n)   { return kmemset(dst, c, n); }
void *memcpy(void *dst, const void *src, size_t n) { return kmemcpy(dst, src, n); }
void *memmove(void *dst, const void *src, size_t n) { return kmemmove(dst, src, n); }
int   memcmp(const void *a, const void *b, size_t n) { return kmemcmp(a, b, n); }
