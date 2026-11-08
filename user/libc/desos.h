#ifndef DESOS_LIBC_H
#define DESOS_LIBC_H

// desos.h —— DesOS 自研 libc（D.1）公共头，用户态程序唯一入口。
//
// 设计约束（freestanding，无宿主 libc）：
//   - 只依赖 gcc 自带的 freestanding 头（stddef/stdint/stdarg）
//   - 所有内核交互都走 int 0x80，调用号来自 kernel/inc/desos_abi.h（唯一出处）
//   - 不做缓冲 I/O（write 直通内核串口），后续 VFS 落地后再扩展 fd 语义

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <desos_abi.h>

// ---------- 系统调用封装 ----------
// 三参数形式（现 ABI 只有 3 个参数寄存器，足够）。
static inline long desos_syscall(long num, long a1, long a2, long a3) {
    long r = num;
    __asm__ volatile ("int $0x80"
                      : "+a"(r)
                      : "D"(a1), "S"(a2), "d"(a3)
                      : "memory");
    return r;
}

typedef long ssize_t;

// 写标准输出（fd 目前只支持 DESOS_STDOUT）。返回写入字节数，失败 -1。
static inline ssize_t write(int fd, const void *buf, size_t len) {
    return desos_syscall(SYS_WRITE, fd, (long)(uintptr_t)buf, (long)len);
}

// 结束当前用户线程（不返回）。
static inline __attribute__((noreturn)) void exit(int code) {
    (void)desos_syscall(SYS_EXIT, code, 0, 0);
    for (;;) { }
}

static inline void yield(void) {
    (void)desos_syscall(SYS_YIELD, 0, 0, 0);
}

// 当前线程 id（内核线程模型下即 pid）。
static inline long getpid(void) {
    return desos_syscall(SYS_GETPID, 0, 0, 0);
}

// 内核 uptime tick（PIT 100 Hz）。
static inline unsigned long sys_ticks(void) {
    return (unsigned long)desos_syscall(SYS_GET_TICKS, 0, 0, 0);
}

// ---------- D.3 服务表查询 ----------
// svc_count()：服务总数。svc_info(idx, buf, size)：拷贝一条服务行
// （"name running kernel"，size>=32），0=成功，-1=非法 idx。
static inline long svc_count(void) {
    return desos_syscall(SYS_SVC_INFO, (long)-1, 0, 0);
}

static inline int svc_info(long idx, char *buf, size_t size) {
    return (int)desos_syscall(SYS_SVC_INFO, idx, (long)(uintptr_t)buf, (long)size);
}

// ---------- <string.h> 子集 ----------
void   *memcpy(void *dst, const void *src, size_t n);
void   *memmove(void *dst, const void *src, size_t n);
void   *memset(void *dst, int c, size_t n);
int     memcmp(const void *a, const void *b, size_t n);
size_t  strlen(const char *s);
int     strcmp(const char *a, const char *b);
int     strncmp(const char *a, const char *b, size_t n);
char   *strchr(const char *s, int c);

// ---------- <stdio.h> 子集 ----------
int  vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int  snprintf(char *buf, size_t size, const char *fmt, ...);
int  vprintf(const char *fmt, va_list ap);
int  printf(const char *fmt, ...);
int  puts(const char *s);
int  putchar(int c);

// ---------- <stdlib.h> 子集 ----------
int atoi(const char *s);
int abs(int v);

#endif // DESOS_LIBC_H
