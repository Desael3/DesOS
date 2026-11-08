// fb.c —— C.2 帧缓冲输出驱动（Limine framebuffer，32bpp）
//
// M1 的临时点阵绘制迁到这里并扩展：完整 5x7 可打印 ASCII 字库、
// 像素/矩形/读回原语，以及把 kprintf 输出镜像到屏幕的简易文本控制台。
// 所有写入都做边界裁剪；fb_pget() 让 -nographic 下的 QEMU 也能做像素级自检。

#include "fb.h"
#include "kstring.h"
#include <stdint.h>

// 5x7 行式点阵：每行低 5 位，bit4 为最左像素。索引 = ASCII - 32。
static const uint8_t font5x7[95][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 32 ' '
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, // 33 '!'
    {0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00}, // 34 '"'
    {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A}, // 35 '#'
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, // 36 '$'
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, // 37 '%'
    {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, // 38 '&'
    {0x0C,0x04,0x08,0x00,0x00,0x00,0x00}, // 39 '\''
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, // 40 '('
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, // 41 ')'
    {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, // 42 '*'
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, // 43 '+'
    {0x00,0x00,0x00,0x00,0x0C,0x04,0x08}, // 44 ','
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, // 45 '-'
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, // 46 '.'
    {0x01,0x01,0x02,0x04,0x08,0x10,0x10}, // 47 '/'
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 48 '0'
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 49 '1'
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, // 50 '2'
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, // 51 '3'
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 52 '4'
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 53 '5'
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // 54 '6'
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // 55 '7'
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 56 '8'
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 57 '9'
    {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}, // 58 ':'
    {0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08}, // 59 ';'
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, // 60 '<'
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, // 61 '='
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, // 62 '>'
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, // 63 '?'
    {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, // 64 '@'
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // 65 'A'
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // 66 'B'
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // 67 'C'
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, // 68 'D'
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // 69 'E'
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // 70 'F'
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, // 71 'G'
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // 72 'H'
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // 73 'I'
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // 74 'J'
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // 75 'K'
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // 76 'L'
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // 77 'M'
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, // 78 'N'
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // 79 'O'
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // 80 'P'
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // 81 'Q'
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // 82 'R'
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // 83 'S'
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // 84 'T'
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // 85 'U'
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // 86 'V'
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, // 87 'W'
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // 88 'X'
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // 89 'Y'
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // 90 'Z'
    {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, // 91 '['
    {0x10,0x10,0x08,0x04,0x02,0x01,0x01}, // 92 '\'
    {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, // 93 ']'
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, // 94 '^'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, // 95 '_'
    {0x08,0x04,0x02,0x00,0x00,0x00,0x00}, // 96 '`'
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, // 97 'a'
    {0x10,0x10,0x16,0x19,0x11,0x11,0x0E}, // 98 'b'
    {0x00,0x00,0x0E,0x11,0x10,0x11,0x0E}, // 99 'c'
    {0x01,0x01,0x0D,0x13,0x11,0x11,0x0F}, // 100 'd'
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, // 101 'e'
    {0x06,0x09,0x08,0x1C,0x08,0x08,0x08}, // 102 'f'
    {0x00,0x0F,0x11,0x11,0x0F,0x01,0x0E}, // 103 'g'
    {0x10,0x10,0x16,0x19,0x11,0x11,0x11}, // 104 'h'
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, // 105 'i'
    {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, // 106 'j'
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12}, // 107 'k'
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, // 108 'l'
    {0x00,0x00,0x1A,0x15,0x15,0x11,0x11}, // 109 'm'
    {0x00,0x00,0x16,0x19,0x11,0x11,0x11}, // 110 'n'
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, // 111 'o'
    {0x00,0x1E,0x11,0x11,0x1E,0x10,0x10}, // 112 'p'
    {0x00,0x0D,0x13,0x11,0x0F,0x01,0x01}, // 113 'q'
    {0x00,0x00,0x16,0x19,0x10,0x10,0x10}, // 114 'r'
    {0x00,0x00,0x0E,0x10,0x0E,0x01,0x1E}, // 115 's'
    {0x08,0x08,0x1C,0x08,0x08,0x09,0x06}, // 116 't'
    {0x00,0x00,0x11,0x11,0x11,0x13,0x0D}, // 117 'u'
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, // 118 'v'
    {0x00,0x00,0x11,0x11,0x15,0x15,0x0A}, // 119 'w'
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, // 120 'x'
    {0x00,0x11,0x11,0x11,0x0F,0x01,0x0E}, // 121 'y'
    {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, // 122 'z'
    {0x02,0x04,0x04,0x08,0x04,0x04,0x02}, // 123 '{'
    {0x04,0x04,0x04,0x04,0x04,0x04,0x04}, // 124 '|'
    {0x08,0x04,0x04,0x02,0x04,0x04,0x08}, // 125 '}'
    {0x00,0x08,0x15,0x02,0x00,0x00,0x00}, // 126 '~'
};

static struct limine_framebuffer *g_fb;
static uint32_t bg_color;
static uint32_t fg_color;

// 控制台状态（基于字符网格，支持像素级平滑动画、无撕裂）
static int      con_inited;
static uint64_t con_x, con_y, con_w, con_h;
static int      con_scale;
static uint64_t con_col, con_row;        // 网格内光标位置
static uint32_t con_total_lines;         // 累计换行数（自检用）
static int      con_smooth;              // 平滑滚动开关

#define CON_MAXR 512
#define CON_MAXC 512
static char     con_grid[CON_MAXR][CON_MAXC];   // 文本网格（行优先）
static uint32_t con_grow;                       // 当前已填充的逻辑行数
static int      con_anim;                        // 动画进行中
static int      con_anim_off;                    // 动画像素偏移（向上滑动时由 cell_h 递减到 0）

void fb_init(struct limine_framebuffer *fb) {
    g_fb = fb;
    bg_color = 0;
    fg_color = 0;
    con_inited = 0;
    con_col = con_row = 0;
    con_total_lines = 0;
    con_grow = 0;
    con_anim = 0;
}

int      fb_ready(void)  { return g_fb != NULL && g_fb->bpp == 32; }
uint64_t fb_width(void)  { return g_fb ? g_fb->width : 0; }
uint64_t fb_height(void) { return g_fb ? g_fb->height : 0; }
uint64_t fb_pitch(void)  { return g_fb ? g_fb->pitch : 0; }
uint16_t fb_bpp(void)    { return g_fb ? g_fb->bpp : 0; }

uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b) {
    if (g_fb == NULL) return 0;
    return ((uint32_t)r << g_fb->red_mask_shift)
         | ((uint32_t)g << g_fb->green_mask_shift)
         | ((uint32_t)b << g_fb->blue_mask_shift);
}

void fb_pset(uint64_t x, uint64_t y, uint32_t color) {
    if (!fb_ready() || x >= g_fb->width || y >= g_fb->height) return;
    uint8_t *base = (uint8_t *)g_fb->address;
    uint64_t off = y * g_fb->pitch + x * 4;
    *(volatile uint32_t *)(base + off) = color;
}

uint32_t fb_pget(uint64_t x, uint64_t y) {
    if (!fb_ready() || x >= g_fb->width || y >= g_fb->height) return 0;
    uint8_t *base = (uint8_t *)g_fb->address;
    uint64_t off = y * g_fb->pitch + x * 4;
    return *(volatile uint32_t *)(base + off);
}

void fb_clear(uint32_t color) {
    if (!fb_ready()) return;
    bg_color = color;
    uint8_t *base = (uint8_t *)g_fb->address;
    for (uint64_t y = 0; y < g_fb->height; y++) {
        uint32_t *row = (uint32_t *)(base + y * g_fb->pitch);
        for (uint64_t x = 0; x < g_fb->width; x++) row[x] = color;
    }
}

void fb_fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color) {
    if (!fb_ready()) return;
    for (uint64_t row = 0; row < h; row++) {
        for (uint64_t col = 0; col < w; col++) {
            fb_pset(x + col, y + row, color);
        }
    }
}

void fb_draw_char(char c, uint64_t x, uint64_t y, uint32_t fg, int scale) {
    unsigned char uc = (unsigned char)c;
    if (!fb_ready() || uc < 32 || uc > 126) return;
    if (scale < 1) scale = 1;
    const uint8_t *g = font5x7[uc - 32];
    for (int r = 0; r < 7; r++) {
        uint8_t line = g[r];
        for (int col = 0; col < 5; col++) {
            if ((line >> (4 - col)) & 1) {
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        fb_pset(x + (uint64_t)col * scale + sx,
                                y + (uint64_t)r * scale + sy, fg);
            }
        }
    }
}

void fb_draw_string(const char *s, uint64_t x, uint64_t y, uint32_t fg, int scale) {
    if (scale < 1) scale = 1;
    uint64_t step = (uint64_t)6 * scale;
    for (; s != NULL && *s != '\0'; s++) {
        fb_draw_char(*s, x, y, fg, scale);
        x += step;
    }
}

static uint64_t cell_w(void) { return (uint64_t)6 * con_scale; }
static uint64_t cell_h(void) { return (uint64_t)8 * con_scale; }
static uint64_t con_cols(void) { return con_w / cell_w(); }
static uint64_t con_lines(void) { return con_h / cell_h(); }

void fb_console_init(uint64_t x, uint64_t y, uint64_t w, uint64_t h, int scale) {
    if (!fb_ready()) return;
    if (scale < 1) scale = 1;
    con_x = x; con_y = y; con_w = w; con_h = h;
    con_scale = scale;
    con_col = con_row = 0;
    con_total_lines = 0;
    con_smooth = 0;
    con_grow = 0;
    con_anim = 0;
    con_anim_off = 0;
    for (uint32_t r = 0; r < CON_MAXR; r++)
        for (uint32_t c = 0; c < CON_MAXC; c++)
            con_grid[r][c] = 0;
    fg_color = fb_rgb(220, 230, 255);
    con_inited = 1;
}

// 按当前像素偏移把网格整屏渲染（off>0 表示整块内容临时下移 off 像素，
// 用于向上滑动的起始帧；每个字形都在整数像素位置整字绘制，故不会撕裂）。
static void fb_console_render(int off) {
    if (!con_inited) return;
    uint8_t *base = (uint8_t *)g_fb->address;
    uint64_t cw = cell_w(), ch = cell_h();
    for (uint64_t y = 0; y < con_h; y++) {
        uint32_t *row = (uint32_t *)(base + (con_y + y) * g_fb->pitch + con_x * 4);
        for (uint64_t x = 0; x < con_w; x++) row[x] = bg_color;
    }
    uint32_t maxr = con_grow < CON_MAXR ? con_grow : CON_MAXR;
    uint32_t maxc = con_cols() < CON_MAXC ? (uint32_t)con_cols() : CON_MAXC;
    for (uint32_t r = 0; r < maxr; r++) {
        uint64_t py = con_y + (uint64_t)r * ch + (uint64_t)off;
        for (uint32_t c = 0; c < maxc; c++) {
            char chc = con_grid[r][c];
            if (chc >= 32 && chc <= 126)
                fb_draw_char(chc, con_x + (uint64_t)c * cw, py, fg_color, con_scale);
        }
    }
}

// 网格整体上移一行（丢弃第 0 行，末行清空）
static void fb_console_grid_up(void) {
    uint32_t maxr = con_grow < CON_MAXR ? con_grow : CON_MAXR;
    uint32_t maxc = con_cols() < CON_MAXC ? (uint32_t)con_cols() : CON_MAXC;
    if (maxr > 1)
        for (uint32_t r = 0; r < maxr - 1; r++)
            kmemmove(con_grid[r], con_grid[r + 1], maxc);
    for (uint32_t c = 0; c < maxc; c++) con_grid[maxr - 1][c] = 0;
}

// 瞬时滚动（自检阶段默认）：显存整块 memmove + 网格同步
static void fb_console_scroll_instant(void) {
    uint64_t ch = cell_h();
    uint64_t lines = con_lines();
    if (lines < 2 || ch == 0) return;
    uint8_t *base = (uint8_t *)g_fb->address;
    uint64_t row_bytes = con_w * 4;
    for (uint64_t p = 0; p < (lines - 1) * ch; p++) {
        kmemmove(base + (con_y + p) * g_fb->pitch + con_x * 4,
                 base + (con_y + p + ch) * g_fb->pitch + con_x * 4,
                 row_bytes);
    }
    fb_fill_rect(con_x, con_y + (lines - 1) * ch, con_w, ch, bg_color);
    fb_console_grid_up();
}

// 平滑动画：每帧上移多像素（非逐像素慢爬），间隔很短，呈高帧率顺滑上滑。
#ifndef FB_SCROLL_DELAY
#define FB_SCROLL_DELAY 6000ULL
#endif
static void fb_console_anim_delay(void) {
    volatile uint64_t i = FB_SCROLL_DELAY;
    while (i--) ;
}
static void fb_console_scroll_smooth(void) {
    fb_console_grid_up();
    con_anim = 1;
    con_anim_off = (int)cell_h();
    int step = (int)cell_h() / 3; if (step < 2) step = 2;
    while (con_anim_off > 0) {
        con_anim_off -= step;
        if (con_anim_off < 0) con_anim_off = 0;
        fb_console_render(con_anim_off);
        fb_console_anim_delay();
    }
    con_anim = 0;
    fb_console_render(0);
}

static void fb_console_scroll(void) {
    // 动画进行中（理论上单线程不会重入）走瞬时路径，避免嵌套动画
    if (con_smooth && !con_anim) fb_console_scroll_smooth();
    else fb_console_scroll_instant();
}

void fb_console_set_smooth(int on) { con_smooth = on; }

void fb_console_putc(char c) {
    if (!fb_ready() || !con_inited) return;
    unsigned char uc = (unsigned char)c;
    if (c == '\r') { con_col = 0; return; } // 回车：回到本行行首（供 Shell 原地重绘当前行）
    uint64_t lines = con_lines();
    uint64_t cols  = con_cols();
    if (lines < 1 || cols < 1) return;

    if (c == '\n') {
        con_col = 0;
        con_row++;
        con_total_lines++;
        if (con_row >= lines) {
            con_row = lines - 1;
            if (con_grow < (uint32_t)lines) con_grow = (uint32_t)lines;
            fb_console_scroll();
        }
        return;
    }
    if (c == '\b') {
        if (con_col > 0) con_col--;
        fb_fill_rect(con_x + con_col * cell_w(), con_y + con_row * cell_h(),
                     cell_w(), cell_h(), bg_color);
        con_grid[con_row][con_col] = 0;
        return;
    }
    if (uc < 32 || uc > 126) return;

    if (con_col >= cols) {                  // 行尾自动换行
        con_col = 0;
        con_row++;
        con_total_lines++;
        if (con_row >= lines) {
            con_row = lines - 1;
            if (con_grow < (uint32_t)lines) con_grow = (uint32_t)lines;
            fb_console_scroll();
        }
    }
    con_grid[con_row][con_col] = c;
    if (con_grow < con_row + 1) con_grow = con_row + 1;
    if (!con_anim) {                        // 动画期间整屏由 render 绘制，无需单字直绘
        uint64_t px = con_x + con_col * cell_w();
        uint64_t py = con_y + con_row * cell_h();
        fb_draw_char(c, px, py, fg_color, con_scale);
    }
    con_col++;
}

void fb_console_clear(void) {
    if (!con_inited) return;
    for (uint32_t r = 0; r < CON_MAXR; r++)
        for (uint32_t c = 0; c < CON_MAXC; c++)
            con_grid[r][c] = 0;
    con_col = con_row = 0;
    con_grow = 0;
    con_total_lines = 0;
    con_anim = 0;
    con_anim_off = 0;
    fb_fill_rect(con_x, con_y, con_w, con_h, bg_color);
}

uint32_t fb_console_rows(void) { return con_total_lines; }
