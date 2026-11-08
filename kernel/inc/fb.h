#ifndef DESOS_FB_H
#define DESOS_FB_H

#include <stdint.h>
#include <limine.h>

// C.2 帧缓冲输出驱动（Limine framebuffer，当前仅支持 32bpp）。
// M1 的临时绘制代码迁到这里；kprintf 会把同一份输出镜像到屏幕控制台。

void     fb_init(struct limine_framebuffer *fb);
int      fb_ready(void);
uint64_t fb_width(void);
uint64_t fb_height(void);
uint64_t fb_pitch(void);
uint16_t fb_bpp(void);

uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b);
void     fb_pset(uint64_t x, uint64_t y, uint32_t color);
uint32_t fb_pget(uint64_t x, uint64_t y);   // 读回显存，供自检验证
void     fb_clear(uint32_t color);
void     fb_fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color);

// 5x7 点阵，覆盖可打印 ASCII 32..126；scale 为像素放大倍数
void     fb_draw_char(char c, uint64_t x, uint64_t y, uint32_t fg, int scale);
void     fb_draw_string(const char *s, uint64_t x, uint64_t y, uint32_t fg, int scale);

// 串口日志镜像用的简易文本控制台：自动换行、满屏滚动
void     fb_console_init(uint64_t x, uint64_t y, uint64_t w, uint64_t h, int scale);
void     fb_console_putc(char c);
void     fb_console_clear(void);            // D.5：清空控制台网格/像素与行列状态
uint32_t fb_console_rows(void);             // 累计换行数（自检用）
void     fb_console_set_smooth(int on);     // 开/关平滑滚动（1=逐像素滑动，0=瞬时整块）

#endif // DESOS_FB_H
