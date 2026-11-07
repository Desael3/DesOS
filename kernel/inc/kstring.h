#ifndef DESOS_KSTRING_H
#define DESOS_KSTRING_H

#include <stddef.h>

/* 内核自有内存操作（B 阶段基础设施，freestanding 环境无 libc）。
 * 同时提供标准名字的弱实现，防止编译器把循环优化成对 memset/memcpy 的调用。 */
void *kmemset(void *dst, int c, size_t n);
void *kmemcpy(void *dst, const void *src, size_t n);
void *kmemmove(void *dst, const void *src, size_t n);
int   kmemcmp(const void *a, const void *b, size_t n);

#endif // DESOS_KSTRING_H
