// xhci.c —— C.6 USB：xHCI 主机控制器 + USB HID 键鼠（最小可用子集）
//
// 仅覆盖 QEMU `qemu-xhci` 与现代机器上的 xHCI（PCI class 0x0C,03,30）：
//   PCI 探测 → 64 位 BAR0 MMIO 映射（uncacheable）→ HCRST → 命令环/事件环/DCBAA
//   设置并 Run → 端口枚举（复位）→ Enable Slot → Address Device（EP0 输入上下文）
//   → EP0 控制传输读描述符 → 识别 HID 键鼠 → Configure Endpoint 建中断端点 →
//   boot 协议 + 中断端点轮询（usb_poll，由 timer tick 调用）。
// HID 键鼠事件喂入既有输入层（shell_input_push / mouse_inject_rel）。

#include "usb.h"
#include "pci.h"
#include "kprintf.h"
#include "pmm.h"
#include "vmm.h"
#include "shell.h"
#include "mouse.h"
#include <stdint.h>
#include <stddef.h>

// ---------------- xHCI 寄存器（相对 MMIO 基址的字节偏移）----------------
#define CAP_LENGTH     0x00
#define HCSPARAMS1     0x04
#define HCSPARAMS2     0x08
#define HCCPARAMS1     0x10

#define OP_USBCMD      0x00
#define OP_USBSTS      0x04
#define OP_PAGESIZE    0x08
#define OP_CRCR        0x18
#define OP_DCBAAP      0x30
#define OP_CONFIG      0x38
#define OP_PORT_BASE   0x400
#define PORT_STRIDE    0x10

// Runtime / Doorbell 基址不固定（QEMU qemu-xhci：MMIO+0x1000 / MMIO+0x2000；
// 其它实现常见 CAPLENGTH+0x600 / CAPLENGTH+0x800），运行时探测确定。
// interrupter0 从 runtime 基址 +0x20 起（前面是 MFINDEX）。
#define RT_IMAN        0x00          // 相对 interrupter0 基址
#define RT_ERSTSZ      0x08
#define RT_ERSTBA      0x10
#define RT_ERDP        0x18

#define USBCMD_RUN   (1u << 0)
#define USBCMD_HCRST (1u << 1)
#define USBSTS_HCH   (1u << 0)
#define USBSTS_CNR   (1u << 11)
#define CRCR_RCS    (1u << 0)
#define CONFIG_MAXSLOTS(n) ((uint32_t)(n) & 0xFF)

#define PORT_CCS    (1u << 0)
#define PORT_PED    (1u << 1)
#define PORT_PR     (1u << 4)
#define PORT_PP     (1u << 9)
#define PORT_CSC    (1u << 17)
#define PORT_PRC    (1u << 21)
#define PORT_PLC    (1u << 22)

// TRB 类型
#define TRB_TYPE(t)         (((t) >> 10) & 0x3F)
#define TRB_NORMAL          1
#define TRB_SETUP           2
#define TRB_DATA            3
#define TRB_STATUS          4
#define TRB_LINK            6
#define TRB_ENABLE_SLOT     9
#define TRB_DISABLE_SLOT    10
#define TRB_ADDRESS_DEVICE  11
#define TRB_CONFIG_EP       12
#define TRB_EV_TRANSFER     32
#define TRB_EV_CMD_COMPLETE 33

#define TRB_C       (1u << 0)
#define TRB_TC      (1u << 1)
#define TRB_IOC     (1u << 5)   // Interrupt On Completion：无此位则 xHC 静默完成、无事件
#define TRB_ENT     (1u << 6)
#define TRB_DIR_IN  (1u << 16)

#define RING_TRBS  256
#define CTX32      32
#define CTX64      64
#define EV_DOORBELL (1u << 3)

typedef struct { uint32_t d[4]; } trb_t;

// DMA 分配（uncacheable 映射，保证 CPU 能看见设备回写）
#define DMA_VA  0xFFFFCD0000000000ULL
static uint64_t dma_next = DMA_VA;
static uint64_t dma_alloc_pages(uint64_t pages, uint64_t *phys_out) {
    uint64_t phys = pmm_alloc_frames(pages);
    if (!phys) return 0;
    uint64_t v = dma_next;
    for (uint64_t i = 0; i < pages; i++) {
        if (vmm_map(v + i * 4096, phys + i * 4096,
                    VMM_PRESENT | VMM_WRITE | VMM_PCD) != 0)
            return 0;
    }
    dma_next += pages * 4096;
    *phys_out = phys;
    return v;
}

// ---------------- 全局状态 ----------------
static int       g_present;
static volatile uint32_t *g_mmio;
static uint32_t  g_caplen;
static uint32_t  g_ctxsize;
static uint32_t  g_max_slots;
static uint32_t  g_nports;
static uint32_t  g_rt0;      // interrupter0 寄存器的 MMIO 偏移（探测得到）
static uint32_t  g_db0;      // doorbell[0] 的 MMIO 偏移（探测得到）

static trb_t    *g_cmd;  uint64_t g_cmd_phys;
static uint32_t  g_cmd_cycle = 1, g_cmd_idx = 0;
static trb_t    *g_ev;   uint64_t g_ev_phys;
static uint32_t  g_ev_cycle = 1, g_ev_idx = 0;
static uint64_t *g_dcbaa; uint64_t g_dcbaa_phys;
static void     *g_dev0ctx; uint64_t g_dev0ctx_phys;
static volatile uint64_t *g_erst; uint64_t g_erst_phys;   // 事件环段表（ERST）

static inline uint32_t rd(uint32_t off) { return g_mmio[off / 4]; }
static inline void     wr(uint32_t off, uint32_t v) { g_mmio[off / 4] = v; }
static inline uint32_t op_r(uint32_t o) { return rd(g_caplen + o); }
static inline void     op_w(uint32_t o, uint32_t v) { wr(g_caplen + o, v); }
static inline void     db_w(uint32_t slot, uint32_t val) {
    volatile uint32_t *db = (volatile uint32_t *)((uint8_t *)g_mmio + g_db0 + 4 * slot);
    *db = val;
}
// 探测 runtime/doorbell 偏移：向候选 ERSTSZ 写 1 并回读验证。
static int probe_offsets(void) {
    static const struct { uint32_t rt, db; } cand[] = {
        { 0x1000 + 0x20, 0x2000 },                       // QEMU qemu-xhci
        { 0, 0 },                                        // 占位：CAPLENGTH 布局
    };
    for (unsigned i = 0; i < 2; i++) {
        uint32_t rt = cand[i].rt, db = cand[i].db;
        if (i == 1) { rt = g_caplen + 0x600 + 0x20; db = g_caplen + 0x800; }
        wr(rt + RT_ERSTSZ, 1);
        if (rd(rt + RT_ERSTSZ) == 1) { g_rt0 = rt; g_db0 = db; return 0; }
    }
    return -1;
}

static void ring_enq(trb_t *ring, uint32_t *idx, uint32_t *cycle,
                     uint32_t d0, uint32_t d1, uint32_t d2, uint32_t d3) {
    uint32_t i = *idx;
    if (i >= RING_TRBS - 1) {
        trb_t *lk = &ring[RING_TRBS - 1];
        lk->d[0] = (uint32_t)((uint64_t)ring & 0xFFFFFFFFu);
        lk->d[1] = (uint32_t)((uint64_t)ring >> 32);
        lk->d[2] = 0;
        lk->d[3] = (*cycle) | TRB_TC | (TRB_LINK << 10);
        *cycle ^= 1;
        i = 0;
    }
    trb_t *t = &ring[i];
    t->d[0] = d0; t->d[1] = d1; t->d[2] = d2;
    t->d[3] = (d3 & ~1u) | (*cycle);
    *idx = i + 1;
}

// 等待指定类型事件；返回事件 TRB（在事件环内），否则 NULL
static trb_t *xhci_wait_event(uint32_t want_type) {
    for (volatile int t = 0; t < 4000000; t++) {
        trb_t *e = &g_ev[g_ev_idx];
        if ((e->d[3] & 1u) != g_ev_cycle) break;     // 无更多有效事件
        uint32_t type = TRB_TYPE(e->d[3]);
        uint32_t nx = g_ev_idx + 1;
        if (nx >= RING_TRBS) { nx = 0; g_ev_cycle ^= 1; }
        g_ev_idx = nx;
        uint64_t erdp = g_ev_phys + (uint64_t)g_ev_idx * 16;
        wr(g_rt0 + RT_ERDP, (uint32_t)(erdp | EV_DOORBELL));
        wr(g_rt0 + RT_ERDP + 4, (uint32_t)(erdp >> 32));
        if (type == want_type) return e;
    }
    return NULL;
}

// ---------------- 命令 ----------------
static uint32_t xhci_cmd_enable_slot(void) {
    ring_enq(g_cmd, &g_cmd_idx, &g_cmd_cycle, 0, 0, 0,
             g_cmd_cycle | (TRB_ENABLE_SLOT << 10));
    db_w(0, 0);
    trb_t *e = xhci_wait_event(TRB_EV_CMD_COMPLETE);
    if (!e) return 0;
    return (e->d[3] >> 24) & 0xFF;       // 槽号在命令完成事件 dword3 高字节
}

// 释放 slot（清 SeaBIOS 等固件留下的占用：slot->uport 不清会触发
// QEMU 的 "port already assigned" 检查 → Address Device 返回 CC_TRB_ERROR）。
// 注意：命令 TRB 的 Slot ID 在控制字（DW3）bits 24-31。
static void xhci_cmd_disable_slot(uint32_t slotid) {
    ring_enq(g_cmd, &g_cmd_idx, &g_cmd_cycle, 0, 0, 0,
             g_cmd_cycle | (TRB_DISABLE_SLOT << 10) | (slotid << 24));
    db_w(0, 0);
    (void)xhci_wait_event(TRB_EV_CMD_COMPLETE);
}

static int xhci_cmd_address_device(uint32_t slot, uint64_t inctx_phys) {
    ring_enq(g_cmd, &g_cmd_idx, &g_cmd_cycle,
             (uint32_t)(inctx_phys & 0xFFFFFFFFu), (uint32_t)(inctx_phys >> 32), 0,
             g_cmd_cycle | (TRB_ADDRESS_DEVICE << 10) | (slot << 24));
    db_w(0, 0);
    trb_t *e = xhci_wait_event(TRB_EV_CMD_COMPLETE);
    if (!e) { kprintf("[C.6] usb: address device: no completion\n"); return -1; }
    uint32_t cc = (e->d[2] >> 24) & 0xFF;
    if (cc != 1) { kprintf("[C.6] usb: address device: CC=%u\n", cc); return -1; }
    return 0;
}

static int xhci_cmd_config_ep(uint32_t slot, uint64_t inctx_phys) {
    ring_enq(g_cmd, &g_cmd_idx, &g_cmd_cycle,
             (uint32_t)(inctx_phys & 0xFFFFFFFFu), (uint32_t)(inctx_phys >> 32), 0,
             g_cmd_cycle | (TRB_CONFIG_EP << 10) | (slot << 24));
    db_w(0, 0);
    trb_t *e = xhci_wait_event(TRB_EV_CMD_COMPLETE);
    if (!e) { kprintf("[C.6] usb: config ep: no completion\n"); return -1; }
    uint32_t cc = (e->d[2] >> 24) & 0xFF;
    if (cc != 1) { kprintf("[C.6] usb: config ep: CC=%u\n", cc); return -1; }
    return 0;
}

// ---------------- 上下文构建 ----------------
static void ctx_w32(uint8_t *ctx, uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(ctx + off) = v;
}
static void ctx_w64(uint8_t *ctx, uint32_t off, uint64_t v) {
    *(volatile uint32_t *)(ctx + off) = (uint32_t)(v & 0xFFFFFFFFu);
    *(volatile uint32_t *)(ctx + off + 4) = (uint32_t)(v >> 32);
}

// Slot Context（xHCI 规范 6.2.2.1 / QEMU+SeaBIOS 实测一致）：
//   DW0: RouteString[0-19] + Speed[20-23] + ContextEntries[27-31]；
//   DW1: MaxExitLatency[0-15] + RootHubPortNumber[16-23]（1-based）；
//   DW2: InterrupterTarget[22-31]（用默认 0）
static void xhci_build_slot_ctx(uint8_t *ctx, uint32_t port, uint32_t speed, uint32_t entries) {
    ctx_w32(ctx, 0, (speed & 0xF) << 20 | (entries & 0x1F) << 27);
    ctx_w32(ctx, 4, ((port + 1) & 0xFF) << 16);              // 根集线器端口号（1-based）
    ctx_w32(ctx, 8, 0);                                      // 中断器目标 = 0
    ctx_w32(ctx, 12, 0);
}
// Endpoint Context：DW0 Interval[16-23]；DW1 EPType[3-5] + MaxPacketSize[16-31]；
// DW2/3 TR环基址（DW2 bit0 = DCS）
static void xhci_build_ep_ctx(uint8_t *ctx, uint32_t type, uint16_t mps,
                              uint32_t interval, uint64_t ring_phys) {
    ctx_w32(ctx, 0, (interval & 0xFF) << 16);
    ctx_w32(ctx, 4, (type << 3) | ((uint32_t)mps << 16));
    ctx_w64(ctx, 8, ring_phys | 1u);                         // DCS=1
    if (g_ctxsize > 32) { ctx_w32(ctx, 16, 0); ctx_w32(ctx, 20, 0); }
}

// ---------------- EP0 控制传输 ----------------
static int xhci_ep0_control(uint32_t slot, trb_t *ring, uint32_t *idx, uint32_t *cycle,
                            uint64_t buf_phys, uint8_t bmReqType, uint8_t bRequest,
                            uint16_t wValue, uint16_t wIndex, uint16_t wLength) {
    int dir_in = (bmReqType & 0x80) ? 1 : 0;
    // setup TRB 参数按小端字节序打包：[bmReqType, bRequest, wValue(2), wIndex(2), wLength(2)]
    // DW0 = wValue<<16 | bRequest<<8 | bmReqType；DW1 = wIndex | wLength<<16
    uint32_t dw0 = ((uint32_t)wValue << 16) | ((uint32_t)bRequest << 8) | (uint32_t)bmReqType;
    uint32_t dw1 = (uint32_t)wIndex | ((uint32_t)wLength << 16);
    uint32_t trt = (wLength > 0) ? (dir_in ? 2 : 3) : 0;   // 0=无数据 2=IN 3=OUT
    ring_enq(ring, idx, cycle, dw0, dw1, 8,
             (*cycle) | TRB_ENT | (trt << 16) | (TRB_SETUP << 10));
    if (wLength > 0) {
        ring_enq(ring, idx, cycle, (uint32_t)(buf_phys & 0xFFFFFFFFu),
                 (uint32_t)(buf_phys >> 32), wLength,
                 (*cycle) | (dir_in ? (TRB_DIR_IN | (TRB_DATA << 10)) : (TRB_DATA << 10)));
    }
    uint32_t sdir = dir_in ? 0 : TRB_DIR_IN;
    ring_enq(ring, idx, cycle, 0, 0, 0,
             (*cycle) | sdir | TRB_IOC | (TRB_STATUS << 10));

    db_w(slot, 1);   // EP0 的 DCI=1

    // 控制传输只在 STATUS TRB（带 IOC）上产生一次完成事件
    int seen = 0, need = 1;
    for (volatile int t = 0; t < 4000000; t++) {
        trb_t *e = &g_ev[g_ev_idx];
        if ((e->d[3] & 1u) != g_ev_cycle) break;
        uint32_t type = TRB_TYPE(e->d[3]);
        uint32_t nx = g_ev_idx + 1;
        if (nx >= RING_TRBS) { nx = 0; g_ev_cycle ^= 1; }
        g_ev_idx = nx;
        uint64_t erdp = g_ev_phys + (uint64_t)g_ev_idx * 16;
        wr(g_rt0 + RT_ERDP, (uint32_t)(erdp | EV_DOORBELL));
        wr(g_rt0 + RT_ERDP + 4, (uint32_t)(erdp >> 32));
        if (type == TRB_EV_TRANSFER) {
            if (++seen >= need) return 0;
        }
    }
    return -1;
}

static int xhci_get_descriptor(uint32_t slot, trb_t *ring, uint32_t *idx, uint32_t *cycle,
                              uint64_t buf_phys, uint8_t type, uint8_t index,
                              uint16_t wIndex, uint16_t len) {
    return xhci_ep0_control(slot, ring, idx, cycle, buf_phys, 0x80, 0x06,
                            (uint16_t)((type << 8) | index), wIndex, len);
}

// ---------------- HID 解码 ----------------
static char hid_to_ascii(uint8_t u) {
    if (u >= 0x04 && u <= 0x1D) return (char)('a' + (u - 0x04));
    if (u >= 0x1E && u <= 0x27) return (char)('1' + (u - 0x1E));
    if (u == 0x28) return '\r';
    if (u == 0x2A) return '\b';
    if (u == 0x2B) return '\t';
    if (u == 0x2C) return ' ';
    if (u == 0x2D) return '-';
    if (u == 0x2E) return '=';
    if (u == 0x2F) return '[';
    if (u == 0x30) return ']';
    if (u == 0x31) return '\\';
    if (u == 0x33) return ';';
    if (u == 0x34) return '\'';
    if (u == 0x35) return '`';
    if (u == 0x36) return ',';
    if (u == 0x37) return '.';
    if (u == 0x38) return '/';
    if (u == 0x4F) return (char)KEY_RIGHT;
    if (u == 0x50) return (char)KEY_LEFT;
    if (u == 0x51) return (char)KEY_DOWN;
    if (u == 0x52) return (char)KEY_UP;
    return 0;
}

// ---------------- 设备表 ----------------
#define MAX_DEV 8
typedef struct {
    int      used;
    uint8_t  slot, port;
    uint16_t vid, pid;
    int      hid_kbd, hid_mouse;
    uint8_t  ep_in;
    uint16_t ep_mps;
    trb_t   *ep0_ring; uint64_t ep0_phys; uint32_t ep0_idx, ep0_cycle;
    trb_t   *intr_ring; uint64_t intr_phys; uint32_t intr_idx, intr_cycle;
    void    *report; uint64_t report_phys;
    int      out_standing;
    uint8_t  last_keys[6];
} usb_dev_t;

static usb_dev_t g_devs[MAX_DEV];
static int       g_ndev, g_nhid;

static usb_dev_t *usb_alloc_dev(void) {
    for (int i = 0; i < MAX_DEV; i++)
        if (!g_devs[i].used) { g_devs[i].used = 1; g_ndev++; return &g_devs[i]; }
    return NULL;
}

static void usb_parse_config(usb_dev_t *d, uint8_t *cfg, uint16_t total) {
    uint16_t off = 0;
    while (off + 2 <= total) {
        uint8_t len = cfg[off], type = cfg[off + 1];
        if (len < 2) break;
        if (type == 0x04) {
            uint8_t cls = cfg[off + 5], sub = cfg[off + 6], proto = cfg[off + 7];
            if (cls == 0x03) {
                if (sub == 0x01 && proto == 0x01) d->hid_kbd = 1;
                else if (sub == 0x01 && proto == 0x02) d->hid_mouse = 1;
            }
        } else if (type == 0x05) {
            uint8_t attr = cfg[off + 3], epaddr = cfg[off + 2];
            uint16_t mps = (uint16_t)cfg[off + 4] | ((uint16_t)cfg[off + 5] << 8);
            if ((attr & 0x03) == 0x03 && (epaddr & 0x80)) {
                d->ep_in = epaddr & 0x0F; d->ep_mps = mps;
            }
        }
        off += len;
    }
}

static void usb_submit_intr(usb_dev_t *d) {
    if (d->ep_in == 0 || !d->report || !d->intr_ring) return;
    uint32_t dci = d->ep_in * 2 + 1;
    ring_enq(d->intr_ring, &d->intr_idx, &d->intr_cycle,
             (uint32_t)(d->report_phys & 0xFFFFFFFFu), (uint32_t)(d->report_phys >> 32),
             d->ep_mps, d->intr_cycle | TRB_IOC | (TRB_NORMAL << 10));
    db_w(d->slot, dci);
    d->out_standing = 1;
}

static void usb_handle_report(usb_dev_t *d, uint8_t *r) {
    if (d->hid_kbd) {
        uint8_t mod = r[0];
        int shift = (mod & 0x22) != 0;
        uint8_t cur[6]; int n = 0;
        for (int i = 2; i < 8 && n < 6; i++) if (r[i]) cur[n++] = r[i];
        for (int i = 0; i < n; i++) {
            int was = 0;
            for (int j = 0; j < 6; j++) if (d->last_keys[j] == cur[i]) { was = 1; break; }
            if (!was) {
                char c = hid_to_ascii(cur[i]);
                if (c == 0) continue;
                if (shift && c >= 'a' && c <= 'z') c = (char)(c - 32);
                shell_input_push(c);
            }
        }
        for (int i = 0; i < 6; i++) d->last_keys[i] = (i < n) ? cur[i] : 0;
    } else if (d->hid_mouse) {
        int8_t dx = (int8_t)r[1], dy = (int8_t)r[2];
        mouse_inject_rel((int32_t)dx, (int32_t)dy, r[0] & 0x07);
    }
}

// ---------------- 端口枚举 ----------------
static void xhci_enumerate_port(uint8_t port) {
    uint32_t poff = OP_PORT_BASE + port * PORT_STRIDE;
    uint32_t ps = op_r(poff);
    if (!(ps & PORT_CCS)) return;
    kprintf("[C.6] usb: port%u: ps=0x%08x (ped=%d pp=%d speed=%u)\n",
            port, ps, !!(ps & PORT_PED), !!(ps & PORT_PP), (ps >> 10) & 0xF);
    if (!(ps & PORT_PED)) {
        op_w(poff, (ps | PORT_PR) & ~PORT_PED);
        for (volatile int i = 0; i < 4000000; i++) if (op_r(poff) & PORT_PRC) break;
        op_w(poff, op_r(poff) | PORT_PRC | PORT_CSC);
        for (volatile int i = 0; i < 500000; i++);
        ps = op_r(poff);
    }
    if (!(ps & PORT_PED)) return;

    uint32_t slot = xhci_cmd_enable_slot();
    if (slot == 0 || slot > g_max_slots) {
        kprintf("[C.6] usb: port%u: enable slot failed (slot=%u)\n", port, slot);
        return;
    }
    kprintf("[C.6] usb: port%u: slot=%u\n", port, slot);
    usb_dev_t *d = usb_alloc_dev();
    if (!d) return;
    d->slot = (uint8_t)slot; d->port = port;
    d->ep0_idx = 0; d->ep0_cycle = 1;

    uint64_t devctx_phys, inctx_phys;
    uint8_t *devctx = (uint8_t *)dma_alloc_pages(1, &devctx_phys);
    uint8_t *inctx  = (uint8_t *)dma_alloc_pages(1, &inctx_phys);
    if (!devctx || !inctx) { d->used = 0; g_ndev--; return; }
    d->ep0_ring = (trb_t *)dma_alloc_pages(1, &d->ep0_phys);
    if (!d->ep0_ring) { d->used = 0; g_ndev--; return; }

    uint32_t speed = (op_r(poff) >> 10) & 0xF;   // PORTSC Speed[10-13]：1=全速 3=高速
    if (speed == 0) speed = 1;
    uint16_t ep0_mps = (speed == 3) ? 64 : 8;    // 高速 EP0 MPS=64，全/低速=8

    // 输入控制上下文：DW0=Drop flags(0)、DW1=Add flags（QEMU 严格校验 0x0/0x3）
    *(volatile uint32_t *)inctx = 0;
    *(volatile uint32_t *)(inctx + 4) = (1u << 0) | (1u << 1);   // Add: A0=Slot + A1=EP0
    xhci_build_slot_ctx(inctx + g_ctxsize, port, speed, 1);
    xhci_build_ep_ctx(inctx + g_ctxsize + g_ctxsize, 4, ep0_mps, 0, d->ep0_phys);
    g_dcbaa[slot] = devctx_phys;

    if (xhci_cmd_address_device(slot, inctx_phys) != 0) { d->used = 0; g_ndev--; return; }
    kprintf("[C.6] usb: port%u slot%u addressed (speed=%u)\n", port, slot, speed);

    uint8_t *scratch; uint64_t sp;
    scratch = (uint8_t *)dma_alloc_pages(1, &sp);
    if (!scratch) { d->used = 0; g_ndev--; return; }

    int r8 = xhci_get_descriptor(slot, d->ep0_ring, &d->ep0_idx, &d->ep0_cycle,
                                 sp, 0x01, 0, 0, 8);
    kprintf("[C.6] usb: slot%u desc8 r=%d mps=%u\n", slot, r8, (uint32_t)scratch[7]);
    if (r8 == 0) {
        uint16_t total = (uint16_t)scratch[0] | ((uint16_t)scratch[1] << 8);
        if (total > 18) total = 18;
        if (xhci_get_descriptor(slot, d->ep0_ring, &d->ep0_idx, &d->ep0_cycle,
                                sp, 0x01, 0, 0, total) == 0) {
            d->vid = (uint16_t)scratch[8] | ((uint16_t)scratch[9] << 8);
            d->pid = (uint16_t)scratch[10] | ((uint16_t)scratch[11] << 8);
            kprintf("[C.6] usb: slot%u dev %04x:%04x (speed=%u)\n",
                    slot, (uint32_t)d->vid, (uint32_t)d->pid, speed);
            uint8_t *cfg; uint64_t cp;
            cfg = (uint8_t *)dma_alloc_pages(1, &cp);
            int rc9 = xhci_get_descriptor(slot, d->ep0_ring, &d->ep0_idx, &d->ep0_cycle,
                                          cp, 0x02, 0, 0, 9);
            kprintf("[C.6] usb: slot%u cfg9 r=%d len=%u cls=%02x sub=%02x proto=%02x\n",
                    slot, rc9, (uint32_t)cfg[2], (uint32_t)cfg[13],
                    (uint32_t)cfg[14], (uint32_t)cfg[15]);
            if (cfg && rc9 == 0) {
                uint16_t ctotal = (uint16_t)cfg[2] | ((uint16_t)cfg[3] << 8);
                uint8_t cfgval = cfg[5];
                if (ctotal > 9) {
                    uint8_t *cfg2; uint64_t cp2;
                    cfg2 = (uint8_t *)dma_alloc_pages(1, &cp2);
                    if (cfg2 && xhci_get_descriptor(slot, d->ep0_ring, &d->ep0_idx, &d->ep0_cycle,
                                    cp2, 0x02, 0, 0, ctotal) == 0)
                        usb_parse_config(d, cfg2, ctotal);
                }
                xhci_ep0_control(slot, d->ep0_ring, &d->ep0_idx, &d->ep0_cycle,
                                cp, 0x00, 0x09, cfgval, 0, 0);             // Set Configuration
                xhci_ep0_control(slot, d->ep0_ring, &d->ep0_idx, &d->ep0_cycle,
                                cp, 0x21, 0x0B, 0, 0, 0);                 // Set Protocol (boot)
                if (d->hid_kbd || d->hid_mouse) {
                    d->intr_ring = (trb_t *)dma_alloc_pages(1, &d->intr_phys);
                    d->intr_idx = 0; d->intr_cycle = 1;
                    if (d->intr_ring && d->ep_in != 0) {
                        uint32_t dci = d->ep_in * 2 + 1;       // IN 端点 DCI
                        uint8_t *ic; uint64_t icp;
                        ic = (uint8_t *)dma_alloc_pages(1, &icp);
                        if (ic) {
                            // 输入控制：DW0=drop 0；DW1=add = A0(Slot) + A<DCI>(端点)
                            *(volatile uint32_t *)ic = 0;
                            *(volatile uint32_t *)(ic + 4) = (1u << 0) | (1u << dci);
                            xhci_build_slot_ctx(ic + g_ctxsize, port, speed, 2);
                            // EP 上下文位于 ictx + ctxsize*(1+DCI)（QEMU：ictx+32+32*i）
                            xhci_build_ep_ctx(ic + g_ctxsize * (1 + dci), 3,
                                              d->ep_mps ? d->ep_mps : 8, 3, d->intr_phys);
                            if (xhci_cmd_config_ep(slot, icp) == 0) {
                                d->report = (uint8_t *)dma_alloc_pages(1, &d->report_phys);
                                if (d->report) { usb_submit_intr(d); g_nhid++; }
                            }
                        }
                    }
                }
            }
        }
    }
}

// ---------------- 初始化 ----------------
void usb_init(void) {
    g_present = 0; g_ndev = 0; g_nhid = 0;
    for (int i = 0; i < MAX_DEV; i++) g_devs[i].used = 0;

    pci_dev_t xhci; int found = 0;
    pci_dev_t all[64]; int cnt = 0;
    if (pci_enumerate(all, 64, &cnt) == 0) {
        for (int i = 0; i < cnt; i++)
            if (all[i].class_base == 0x0C && all[i].class_sub == 0x03 && all[i].class_pi == 0x30) {
                xhci = all[i]; found = 1; break;
            }
    }
    if (!found && pci_find_class(0x0C, 0x03, &xhci) && xhci.class_pi == 0x30) found = 1;
    if (!found) {
        kprintf("[C.6] usb: no xHCI controller (class 0C0300) found\n");
        return;
    }
    kprintf("[C.6] usb: xHCI at %02x:%02x.%u vend=%04x dev=%04x\n",
            xhci.bus, xhci.dev, xhci.fn, xhci.vendor, xhci.device);

    uint32_t pc = pci_read32(xhci.bus, xhci.dev, xhci.fn, 0x04);
    pci_write32(xhci.bus, xhci.dev, xhci.fn, 0x04, pc | 0x06);   // MMIO + 总线主控

    uint32_t bar0 = pci_read32(xhci.bus, xhci.dev, xhci.fn, 0x10);
    uint64_t mmio_phys;
    if ((bar0 & 0x7) == 0x4) {
        uint32_t bar1 = pci_read32(xhci.bus, xhci.dev, xhci.fn, 0x14);
        mmio_phys = ((uint64_t)bar1 << 32) | (uint64_t)(bar0 & ~0xFu);
    } else {
        mmio_phys = (uint64_t)(bar0 & ~0xFu);
    }

    for (uint64_t i = 0; i < 16; i++) {
        if (vmm_map(DMA_VA - 0x10000 + i * 4096, mmio_phys + i * 4096,
                    VMM_PRESENT | VMM_WRITE | VMM_PCD) != 0) {
            kprintf("[C.6] usb: MMIO map failed\n");
            return;
        }
    }
    g_mmio = (volatile uint32_t *)(DMA_VA - 0x10000);

    g_caplen = rd(CAP_LENGTH) & 0xFF;
    uint32_t hcs1 = rd(HCSPARAMS1), hcc1 = rd(HCCPARAMS1);
    g_max_slots = hcs1 & 0xFF;
    g_nports = (hcs1 >> 24) & 0xFF;
    g_ctxsize = (hcc1 & (1u << 2)) ? CTX64 : CTX32;

    op_w(OP_USBCMD, op_r(OP_USBCMD) | USBCMD_HCRST);
    for (volatile int i = 0; i < 4000000; i++)
        if (!(op_r(OP_USBSTS) & USBSTS_CNR) && !(op_r(OP_USBCMD) & USBCMD_HCRST)) break;

    uint64_t v;
    v = dma_alloc_pages(1, &g_cmd_phys);  g_cmd = (trb_t *)v;
    v = dma_alloc_pages(1, &g_ev_phys);   g_ev = (trb_t *)v;
    v = dma_alloc_pages(1, &g_dcbaa_phys); g_dcbaa = (uint64_t *)v;
    v = dma_alloc_pages(1, &g_dev0ctx_phys); g_dev0ctx = (void *)v;
    if (!g_cmd || !g_ev || !g_dcbaa || !g_dev0ctx) {
        kprintf("[C.6] usb: ring alloc failed\n"); return;
    }
    {   // 事件环段表：ERSTBA 必须指向段表（16 字节/项：段基址 + 段大小），不是环本身
        uint64_t v2 = dma_alloc_pages(1, &g_erst_phys);
        g_erst = (volatile uint64_t *)v2;
        if (!g_erst) { kprintf("[C.6] usb: erst alloc failed\n"); return; }
        g_erst[0] = g_ev_phys;
        g_erst[1] = RING_TRBS;
    }

    *(volatile uint64_t *)g_dev0ctx = (uint64_t)g_max_slots;
    g_dcbaa[0] = g_dev0ctx_phys;

    wr(g_caplen + OP_CRCR, (uint32_t)(g_cmd_phys | CRCR_RCS));
    wr(g_caplen + OP_CRCR + 4, (uint32_t)(g_cmd_phys >> 32));
    wr(g_caplen + OP_DCBAAP, (uint32_t)(g_dcbaa_phys & 0xFFFFFFFFu));
    wr(g_caplen + OP_DCBAAP + 4, (uint32_t)(g_dcbaa_phys >> 32));
    op_w(OP_CONFIG, CONFIG_MAXSLOTS(g_max_slots));

    // 探测 runtime/doorbell 偏移，再配置事件环（interrupter0）
    if (probe_offsets() != 0) {
        kprintf("[C.6] usb: runtime register offset probe failed\n");
        return;
    }
    kprintf("[C.6] usb: rt0=0x%x db0=0x%x\n", g_rt0, g_db0);
    wr(g_rt0 + RT_ERSTSZ, 1);
    wr(g_rt0 + RT_ERSTBA, (uint32_t)(g_erst_phys & 0xFFFFFFFFu));
    wr(g_rt0 + RT_ERSTBA + 4, (uint32_t)(g_erst_phys >> 32));
    wr(g_rt0 + RT_ERDP, (uint32_t)(g_ev_phys & 0xFFFFFFFFu));
    wr(g_rt0 + RT_ERDP + 4, (uint32_t)(g_ev_phys >> 32));

    op_w(OP_USBCMD, op_r(OP_USBCMD) | USBCMD_RUN);
    for (volatile int i = 0; i < 4000000; i++) if (!(op_r(OP_USBSTS) & USBSTS_HCH)) break;

    g_present = 1;
    kprintf("[C.6] usb: ready (ports=%u slots=%u ctx=%uB)\n", g_nports, g_max_slots, g_ctxsize);

    // 清理固件（SeaBIOS/UEFI）枚举残留：逐个 Disable Slot 释放端口归属
    for (uint32_t s = 1; s <= g_max_slots && s <= 64; s++)
        xhci_cmd_disable_slot(s);

    for (uint8_t port = 0; port < g_nports; port++) xhci_enumerate_port(port);
    kprintf("[C.6] usb: devices=%d hid=%d\n", g_ndev, g_nhid);
}

int usb_present(void)      { return g_present; }
int usb_device_count(void) { return g_ndev; }
int usb_hid_count(void)    { return g_nhid; }

// timer tick 调用：非阻塞处理事件环，重投 HID 中断传输
void usb_poll(void) {
    if (!g_present) return;
    for (int guard = 0; guard < 64; guard++) {
        trb_t *e = &g_ev[g_ev_idx];
        if ((e->d[3] & 1u) != g_ev_cycle) break;
        uint32_t type = TRB_TYPE(e->d[3]);
        uint32_t nx = g_ev_idx + 1;
        if (nx >= RING_TRBS) { nx = 0; g_ev_cycle ^= 1; }
        g_ev_idx = nx;
        uint64_t erdp = g_ev_phys + (uint64_t)g_ev_idx * 16;
        wr(g_rt0 + RT_ERDP, (uint32_t)(erdp | EV_DOORBELL));
        wr(g_rt0 + RT_ERDP + 4, (uint32_t)(erdp >> 32));
        if (type == TRB_EV_TRANSFER) {
            for (int i = 0; i < MAX_DEV; i++) {
                usb_dev_t *d = &g_devs[i];
                if (d->used && d->out_standing && d->report) {
                    usb_handle_report(d, (uint8_t *)d->report);
                    d->out_standing = 0;
                    usb_submit_intr(d);
                    break;
                }
            }
        }
    }
}
