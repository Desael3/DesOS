// kernel_main.c —— DesOS 内核入口（对应 ProjectRoad A.3）
//
// 通过 Limine 原生协议的「请求/响应」模型拿到帧缓冲，然后在上面画字，
// 显示 "DesOS boot OK"，证明内核已成功接管控制权（里程碑 M1）。
//
// 约定（取自 tools/limine/limine-12.9.6/limine-protocol/）：
//   - 所有指针（含 framebuffer.address）已由 Limine 加上 HHDM 偏移，可直接解引用。
//   - 请求结构体放在 .limine_reqs 段，Limine 扫描后填充 .response。

#include <stddef.h>
#include <stdint.h>
#include <limine.h>
#include "serial.h"
#include "kprintf.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "apic.h"
#include "sched.h"
#include "timer.h"
#include "syscall.h"
#include "shell.h"
#include "b_selftest.h"

// ===== Limine 请求 =====
// 重要：段名绝不能叫 ".limine_reqs"。对 base revision 0，Limine 会把同名段
// 当作「NULL 结尾的请求指针数组」（已废弃的机制）来解释，而我们这里放的是
// 请求结构本身，那些 64 位 magic 会被当成越界指针，导致引导时 panic：
//   "limine: .limine_reqs entry outside kernel image"  -> 内核永远不会被跳转。
// 改用普通段名，Limine 就走标准的「扫描已加载镜像 + 请求分隔符」路径。
//
// 分隔符也限定 base revision tag 的搜索范围（协议原文：delimiters 覆盖
// "requests (including base revision tags)"），所以 base revision tag 必须
// 放在 start/end marker 之间。
__attribute__((used, section(".limine_reqs_data")))
static volatile uint64_t limine_reqs_start[4] = LIMINE_REQUESTS_START_MARKER;

// base revision 1：已移除废弃的 .limine_reqs 指针数组机制与恒等映射。
// 本内核只用 HHDM 指针访问帧缓冲、用端口 I/O 打印串口，不依赖恒等映射。
__attribute__((used, section(".limine_reqs_data")))
static volatile uint64_t limine_base_revision[3] = LIMINE_BASE_REVISION(1);

__attribute__((used, section(".limine_reqs_data")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
};

// B.1：向 Limine 要「物理内存映射表」与「HHDM 偏移」。
// 二者都必须在请求分隔符之间，才会被 Limine 扫描到并回填 response。
__attribute__((used, section(".limine_reqs_data")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_reqs_data")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
};

// B.6b：ACPI RSDP 请求（base revision 1 下返回的是 HHDM 虚拟地址）。
// 内核自检里用它找 MADT，再定位 LAPIC / IOAPIC。
__attribute__((used, section(".limine_reqs_data")))
static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_reqs_data")))
static volatile uint64_t limine_reqs_end[2] = LIMINE_REQUESTS_END_MARKER;

// ===== 极小 5x7 点阵字体（仅覆盖 "DesOS boot OK" 需要的字符）=====
// 每个字形 7 行，每行 5 位（bit4 为最左像素）。其余字符留空（跳过）。
// 注：这是 M1 临时字体，后续 F 阶段会用更完整的字体替换。
static const uint8_t font_bitmaps[][7] = {
    /* 0 'D' */ { 0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11111 },
    /* 1 'e' */ { 0b01110, 0b10001, 0b11111, 0b10000, 0b10000, 0b10001, 0b01110 },
    /* 2 's' */ { 0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110 },
    /* 3 'O' */ { 0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110 },
    /* 4 'S' */ { 0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110 },
    /* 5 'b' */ { 0b10001, 0b10001, 0b10001, 0b11111, 0b10011, 0b10011, 0b11111 },
    /* 6 'o' */ { 0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110 },
    /* 7 't' */ { 0b00100, 0b00100, 0b11111, 0b00100, 0b00100, 0b00100, 0b00110 },
    /* 8 'K' */ { 0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001 },
};

// 字符 -> 字形索引（不在字体里的返回 NULL，绘制时跳过）。
static const uint8_t *get_glyph(char c) {
    switch (c) {
        case 'D': return font_bitmaps[0];
        case 'e': return font_bitmaps[1];
        case 's': return font_bitmaps[2];
        case 'O': return font_bitmaps[3];
        case 'S': return font_bitmaps[4];
        case 'b': return font_bitmaps[5];
        case 'o': return font_bitmaps[6];
        case 't': return font_bitmaps[7];
        case 'K': return font_bitmaps[8];
        default:  return NULL; // 空格及其他字符：留空
    }
}

// 依据帧缓冲的 RGB 掩码移位拼出像素值（与具体像素排列无关）。
static uint32_t rgb(struct limine_framebuffer *fb, uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << fb->red_mask_shift)
         | ((uint32_t)g << fb->green_mask_shift)
         | ((uint32_t)b << fb->blue_mask_shift);
}

static void fb_pset(struct limine_framebuffer *fb, uint64_t x, uint64_t y, uint32_t color) {
    if (x >= fb->width || y >= fb->height) return;
    uint8_t *base = (uint8_t *)fb->address;
    uint64_t off = y * fb->pitch + x * (fb->bpp / 8);
    *(uint32_t *)(base + off) = color;
}

static void fb_clear(struct limine_framebuffer *fb, uint32_t color) {
    uint8_t *base = (uint8_t *)fb->address;
    for (uint64_t y = 0; y < fb->height; y++) {
        uint32_t *row = (uint32_t *)(base + y * fb->pitch);
        for (uint64_t x = 0; x < fb->width; x++) row[x] = color;
    }
}

static void fb_draw_char(struct limine_framebuffer *fb, const uint8_t *glyph,
                         uint64_t x, uint64_t y, uint32_t fg, int scale) {
    for (int r = 0; r < 7; r++) {
        uint8_t line = glyph[r];
        for (int c = 0; c < 5; c++) {
            if ((line >> (4 - c)) & 1) {
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        fb_pset(fb, x + (uint64_t)c * scale + sx,
                                    y + (uint64_t)r * scale + sy, fg);
            }
        }
    }
}

static void fb_draw_string(struct limine_framebuffer *fb, const char *s,
                           uint64_t x, uint64_t y, uint32_t fg, int scale) {
    uint64_t cx = x;
    for (; *s; s++) {
        const uint8_t *g = get_glyph(*s);
        if (g) fb_draw_char(fb, g, cx, y, fg, scale);
        cx += (uint64_t)(5 * scale + scale); // 字宽 5*scale + 1*scale 间距
    }
}

static void halt(void) {
    for (;;) __asm__ volatile ("hlt");
}

// ===== 串口日志 =====
// 正式实现见 kernel/src/serial.c（对应 ProjectRoad A.8）。

void kernel_main(void) {
    // 早期确认：内核已接管控制权（即便后续帧缓冲异常，也能在串口看到，便于验证）。
    // 注：QEMU 的 SeaBIOS 已把 COM1 设成 115200 8N1，无需再初始化；
    // 调 serial_init() 反而会卡在 LSR 等待（见下方排查），故此处直接发。
    serial_puts("DesOS boot OK\n");

    // 必须拿到帧缓冲响应，否则停机。
    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1) {
        halt();
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    if (fb->bpp != 32) {
        halt(); // 当前仅支持 32bpp（A.1 已固定 RESOLUTION=...x32）
    }

    uint32_t bg = rgb(fb, 16, 24, 48);     // 深蓝灰背景
    uint32_t fg = rgb(fb, 220, 230, 255);  // 浅色文字

    fb_clear(fb, bg);
    fb_draw_string(fb, "DesOS boot OK", 80, 80, fg, 4);

    // ===== 阶段 B（B.1–B.11：PMM/VMM/堆、GDT/IDT/PIC+APIC、调度/线程/
    //      系统调用、用户态 ELF/ring3、最小 Shell）=====
    // 帧缓冲画面完成后再跑内存自检：即便自检出问题，屏幕上仍保留 M1 画面，
    // 串口日志则能定位到具体是哪一步失败。
    struct limine_memmap_response *mm =
        (struct limine_memmap_response *)memmap_request.response;
    uint64_t hhdm = hhdm_request.response ? hhdm_request.response->offset : 0;

    if (mm == NULL || hhdm == 0) {
        kprintf("B-STAGE: FAIL (missing Limine memmap/HHDM response)\n");
    } else {
        pmm_init(mm, hhdm);
        if (pmm_hhdm_offset() == 0) {
            kprintf("B-STAGE: FAIL (pmm_init)\n");
        } else {
            vmm_init();
            heap_init();
            apic_set_rsdp(rsdp_request.response
                          ? (uint64_t)(uintptr_t)rsdp_request.response->address : 0,
                          hhdm);
            gdt_init();     // B.4：换上自有 GDT/TSS（含 CS 重载）
            idt_init();     // B.5：装 IDT（selector 用新 GDT 的 0x08）
            pic_init();     // B.6：8259A 重映射，先全屏蔽
            sched_init();   // B.7/B.8：把当前执行流包成 main 线程
            timer_init(100);
            syscall_init(); // B.9：int 0x80 分发表
            b_selftest_run();
            shell_run();    // B.11：进入交互 Shell（不返回）
        }
    }

    halt();
}
