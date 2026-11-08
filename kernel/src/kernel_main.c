// kernel_main.c —— DesOS 内核入口（对应 ProjectRoad A.3）
//
// 通过 Limine 原生协议的「请求/响应」模型拿到帧缓冲，然后在上面画字，
// 显示 "DesOS boot OK"，证明内核已成功接管控制权（里程碑 M1）。
// C.2：绘制代码已迁到 kernel/src/fb.c，这里只做初始化与编排。
//
// 约定（取自 tools/limine/limine-12.9.6/limine-protocol/）：
//   - 所有指针（含 framebuffer.address）已由 Limine 加上 HHDM 偏移，可直接解引用。
//   - 请求结构体放在 .limine_reqs_data 段，Limine 扫描后填充 .response。

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
#include "mouse.h"
#include "pci.h"
#include "ata.h"
#include "net.h"
#include "usb.h"
#include "acpi.h"
#include "fb.h"
#include "service.h"
#include "init.h"
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

static void halt(void) {
    for (;;) __asm__ volatile ("hlt");
}

// ===== 串口日志 =====
// 正式实现见 kernel/src/serial.c（对应 ProjectRoad A.8）。

void kernel_main(void) {
    // 早期确认：内核已接管控制权（即便后续帧缓冲异常，也能在串口看到，便于验证）。
    // 注：QEMU 的 SeaBIOS 已把 COM1 设成 115200 8N1，无需再初始化；
    // 调 serial_init() 反而会卡在 LSR 等待，故此处直接发。
    serial_puts("DesOS boot OK\n");

    // 必须拿到帧缓冲响应，否则停机。
    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1) {
        halt();
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    if (fb->bpp != 32) {
        halt(); // C.2 当前仅支持 32bpp（A.1 已固定 RESOLUTION=...x32）
    }

    // C.2：初始化帧缓冲驱动，画 M1 画面，再把 kprintf 日志镜像到大字下方。
    fb_init(fb);
    uint32_t bg = fb_rgb(16, 24, 48);      // 深蓝灰背景
    uint32_t fg = fb_rgb(220, 230, 255);   // 浅色文字
    fb_clear(bg);
    fb_draw_string("DesOS boot OK", 80, 80, fg, 4);
    uint64_t con_w = fb->width > 80 ? fb->width - 80 : fb->width;
    uint64_t con_h = fb->height > 170 ? fb->height - 170 : fb->height / 2;
    fb_console_init(40, 150, con_w, con_h, 2);

    // D.5 默认静默启动：自检与命令演示只写 COM1 串口（日志完整保留），
    // 不把几十行日志刷到屏幕。进交互 Shell 前再清屏并恢复镜像。
    kprintf_set_quiet(1);
    kprintf("[D.5] console: quiet boot (serial log retained)\n");

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
            // C.7：解析 ACPI（RSDP -> XSDT/RSDT -> FADT/DSDT），
            // 供 B.6b 的 APIC/MADT 与电源管理（poweroff/reboot）共用。
            acpi_init(rsdp_request.response
                      ? (uint64_t)(uintptr_t)rsdp_request.response->address : 0,
                      hhdm);
            gdt_init();     // B.4：换上自有 GDT/TSS（含 CS 重载）
            idt_init();     // B.5：装 IDT（selector 用新 GDT 的 0x08）
            pic_init();     // B.6：8259A 重映射，先全屏蔽
            sched_init();   // B.7/B.8：把当前执行流包成 main 线程
            timer_init(100);
            syscall_init(); // B.9：int 0x80 分发表
            pci_init();     // C.3：探测 PCI 配置机制（枚举在自检内做）
            ata_init();      // C.4：探测 IDE 主盘（硬盘）
            net_init();      // C.5：探测并初始化 e1000 以太网控制器
            usb_init();      // C.6：探测 xHCI 并枚举 USB 键鼠
            service_init();  // D.3：注册内置服务表（Shell services / SYS_SVC_INFO 可查）
            b_selftest_run();
            init_launch();  // D.3b：开机拉起用户态 init（PRIV_SYSTEM），汇报服务表
            mouse_init();   // C.1c：自检完成后启用 PS/2 鼠标 / IRQ12

            // D.5：重画干净的欢迎画面——清掉自检期间 fb_pset/fb_fill_rect
            // 留下的像素测试痕迹，清控制台网格，再恢复屏幕镜像。
            fb_clear(bg);
            fb_draw_string("DesOS boot OK", 80, 80, fg, 4);
            fb_console_clear();
            kprintf_set_quiet(0);
            fb_console_set_smooth(1);   // C.2+：进入交互 Shell 后开平滑滚动
            shell_run();    // B.11：进入交互 Shell（不返回）
        }
    }

    halt();
}
