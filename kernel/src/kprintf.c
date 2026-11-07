// kprintf.c —— 内核轻量格式化输出到 COM1（对应 ProjectRoad B 阶段基础设施）

#include "kprintf.h"
#include "serial.h"
#include <stdarg.h>
#include <stdint.h>

static void kputc(char c) {
    if (c == '\n') serial_putc('\r');
    serial_putc(c);
}

static void put_hex(uint64_t v, int width, int upper) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[20];
    int i = 0;
    if (width > 16) width = 16;
    do {
        buf[i++] = digits[v & 0xF];
        v >>= 4;
    } while (v != 0);
    while (i < width) buf[i++] = '0';
    while (i > 0) kputc(buf[--i]);
}

static void put_dec(uint64_t v, int width) {
    char buf[24];
    int i = 0;
    if (width > 20) width = 20;
    do {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v != 0);
    while (i < width) buf[i++] = '0';
    while (i > 0) kputc(buf[--i]);
}

static void put_signed(int64_t v) {
    if (v < 0) {
        kputc('-');
        // 避免对 INT64_MIN 取负溢出
        put_dec((uint64_t)(-(v + 1)) + 1, 0);
    } else {
        put_dec((uint64_t)v, 0);
    }
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    for (const char *p = fmt; *p != '\0'; p++) {
        if (*p != '%') {
            kputc(*p);
            continue;
        }
        p++;
        if (*p == '\0') break;
        if (*p == '%') {
            kputc('%');
            continue;
        }

        int width = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }
        int longs = 0;
        while (*p == 'l') {
            longs++;
            p++;
        }

        switch (*p) {
            case 'c':
                kputc((char)va_arg(ap, int));
                break;
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (s == NULL) s = "(null)";
                for (; *s != '\0'; s++) kputc(*s);
                break;
            }
            case 'd':
                put_signed(longs ? va_arg(ap, int64_t) : (int64_t)va_arg(ap, int));
                break;
            case 'u':
                put_dec(longs ? va_arg(ap, uint64_t)
                              : (uint64_t)va_arg(ap, unsigned int), width);
                break;
            case 'p':
                kputc('0');
                kputc('x');
                put_hex((uint64_t)(uintptr_t)va_arg(ap, void *), width ? width : 16, 0);
                break;
            case 'x':
            case 'X':
                put_hex(longs ? va_arg(ap, uint64_t)
                              : (uint64_t)va_arg(ap, unsigned int),
                        width, *p == 'X');
                break;
            default:
                kputc('%');
                kputc(*p);
                break;
        }
    }

    va_end(ap);
}
