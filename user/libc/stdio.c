// stdio.c —— DesOS libc <stdio.h> 子集（D.1，freestanding 自研）
//
// 核心：vsnprintf 支持的转换说明
//   %[0][width]<len><conv>，len ∈ { 无, l, ll, z }，conv ∈ { d i u x X s c p % }
//   - %d/%i 有符号十进制；%u 无符号；%x/%X 十六进制；%p 0x + 16 位零填充十六进制
//   - %s NUL 结尾字符串（NULL 打印 "(null)"）；%c 单字符
//   - 截断语义同 C 标准：返回值是"应有长度"，缓冲只写 size-1 字符 + NUL
//
// 输出走 write(DESOS_STDOUT) 直通内核串口，无用户态缓冲层。
#include "desos.h"

struct outbuf {
    char  *p;
    size_t cap;    // 缓冲总容量（含 NUL 位）
    size_t len;    // 已产出字符数（可超过 cap-1，用于返回值）
};

static void ob_putc(struct outbuf *o, char c) {
    if (o->p != NULL && o->len + 1 < o->cap) o->p[o->len] = c;
    o->len++;
}

static const char DIGITS_LO[] = "0123456789abcdef";
static const char DIGITS_UP[] = "0123456789ABCDEF";

static void fmt_unsigned(struct outbuf *o, unsigned long long v,
                         unsigned base, int upper, int width, char pad) {
    char tmp[24];                       // 2^64 十进制最长 20 位
    const char *dig = upper ? DIGITS_UP : DIGITS_LO;
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v != 0) {
        tmp[n++] = dig[v % base];
        v /= base;
    }
    while (n < width) { ob_putc(o, pad); width--; }
    while (n > 0) ob_putc(o, tmp[--n]);
}

static void fmt_signed(struct outbuf *o, long long v, int width, char pad) {
    unsigned long long mag;
    if (v < 0) {
        ob_putc(o, '-');
        mag = (unsigned long long)(-(v + 1)) + 1;   // 防 LLONG_MIN 溢出
        if (width > 0) width--;
    } else {
        mag = (unsigned long long)v;
    }
    fmt_unsigned(o, mag, 10, 0, width, pad);
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    struct outbuf o = { buf, size, 0 };

    for (const char *p = fmt; *p != '\0'; p++) {
        if (*p != '%') { ob_putc(&o, *p); continue; }
        p++;

        if (*p == '%') { ob_putc(&o, '%'); continue; }

        int  width = 0;
        char pad   = ' ';
        if (*p == '0') { pad = '0'; p++; }
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        int lcount = 0;                     // 0=默认 1=long 2=long long（z 同 long）
        while (*p == 'l' || *p == 'z') {
            if (*p == 'l') lcount++;
            else if (lcount < 1) lcount = 1;
            p++;
        }

        switch (*p) {
            case 'd':
            case 'i': {
                long long v = (lcount >= 2) ? va_arg(ap, long long)
                            : (lcount == 1) ? (long long)va_arg(ap, long)
                                            : (long long)va_arg(ap, int);
                fmt_signed(&o, v, width, pad);
                break;
            }
            case 'u': {
                unsigned long long v = (lcount >= 2) ? va_arg(ap, unsigned long long)
                                     : (lcount == 1) ? (unsigned long long)va_arg(ap, unsigned long)
                                                     : (unsigned long long)va_arg(ap, unsigned int);
                fmt_unsigned(&o, v, 10, 0, width, pad);
                break;
            }
            case 'x':
            case 'X': {
                unsigned long long v = (lcount >= 2) ? va_arg(ap, unsigned long long)
                                     : (lcount == 1) ? (unsigned long long)va_arg(ap, unsigned long)
                                                     : (unsigned long long)va_arg(ap, unsigned int);
                fmt_unsigned(&o, v, 16, *p == 'X', width, pad);
                break;
            }
            case 'p': {
                void *v = va_arg(ap, void *);
                ob_putc(&o, '0');
                ob_putc(&o, 'x');
                fmt_unsigned(&o, (unsigned long long)(uintptr_t)v, 16, 0, 16, '0');
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (s == NULL) s = "(null)";
                while (*s != '\0') ob_putc(&o, *s++);
                break;
            }
            case 'c': {
                ob_putc(&o, (char)va_arg(ap, int));
                break;
            }
            default:
                ob_putc(&o, '%');
                ob_putc(&o, *p);
                break;
        }
        if (*p == '\0') break;              // 格式串意外结束
    }

    // NUL 结尾：只有 len < cap-1 时才真的写 NUL 位
    if (buf != NULL && size > 0) {
        size_t end = (o.len < size - 1) ? o.len : size - 1;
        buf[end] = '\0';
    }
    return (int)o.len;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

#define PRINTF_BUFSZ 1024

static int vprintf_out(const char *fmt, va_list ap) {
    char buf[PRINTF_BUFSZ];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    if (n < 0) n = 0;
    return (int)write(DESOS_STDOUT, buf, (size_t)n);
}

int vprintf(const char *fmt, va_list ap) {
    return vprintf_out(fmt, ap);
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vprintf_out(fmt, ap);
    va_end(ap);
    return r;
}

int puts(const char *s) {
    size_t n = strlen(s);
    ssize_t w = write(DESOS_STDOUT, s, n);
    if (w < 0) return -1;
    w += write(DESOS_STDOUT, "\n", 1);
    return (int)w;
}

int putchar(int c) {
    char ch = (char)c;
    return (int)write(DESOS_STDOUT, &ch, 1);
}
