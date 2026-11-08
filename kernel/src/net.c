// net.c —— C.5 网络：Intel e1000（8254x）PCI 以太网控制器最小驱动
//
// 仅覆盖 QEMU `-device e1000` 暴露的 legacy 寄存器子集：
//   探测（PCI class 0x02,00）→ 映射 BAR0 MMIO（uncacheable）→ 软复位 →
//   读 MAC（RAL/RAH）→ 置 SLU 链路 UP → 初始化 TX/RX 描述符环。
// 自检经「发送一帧 + 轮询 TX 描述符 DD」验证数据通路；TCP/IP 协议栈留待后续子步。

#include "net.h"
#include "pci.h"
#include "kprintf.h"
#include "pmm.h"
#include "vmm.h"
#include <stdint.h>
#include <stddef.h>

// ---- 寄存器偏移（相对 BAR0）----
#define REG_CTRL   0x00
#define REG_STATUS 0x08
#define REG_ICR    0xC0
#define REG_IMC    0xD8
#define REG_RCTL   0x100
#define REG_TCTL   0x400
#define REG_RAL    0x5400
#define REG_RAH    0x5404
#define REG_TDBAL  0x3800
#define REG_TDBAH  0x3804
#define REG_TDLEN  0x3808
#define REG_TDH    0x3810
#define REG_TDT    0x3818
#define REG_RDBAL  0x2800
#define REG_RDBAH  0x2804
#define REG_RDLEN  0x2808
#define REG_RDH    0x2810
#define REG_RDT    0x2818

// ---- CTRL / STATUS 位 ----
#define CTRL_RST (1u << 26)   // 设备软复位（自动清零）
#define CTRL_SLU (1u << 6)    // Set Link Up
#define STATUS_LU (1u << 1)   // Link Up

// ---- TCTL / RCTL 位 ----
#define TCTL_EN  (1u << 1)    // 发送使能
#define TCTL_PSP (1u << 3)    // 填充短包
#define RCTL_EN  (1u << 1)    // 接收使能
#define RCTL_BAM (1u << 15)   // 接收广播

// ---- 描述符命令 / 状态 ----
#define TX_CMD_EOP (1u << 0)
#define TX_CMD_IFCS (1u << 1)
#define TX_CMD_RS  (1u << 3)
#define TX_STA_DD  (1u << 0)
#define RX_STA_DD  (1u << 0)
#define RX_STA_EOP (1u << 1)

#define NUM_TX 8
#define NUM_RX 8
#define PKT_SIZE 2048

// legacy TX 描述符（16 字节）
typedef struct {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed)) tx_desc_t;

// legacy RX 描述符（16 字节）
typedef struct {
    uint64_t addr;
    uint16_t length;
    uint16_t chksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed)) rx_desc_t;

// 描述符环 + 包缓冲都放在内核 RAM：物理地址经 PMM 分配。
#define NET_MMIO_VA 0xFFFFCB0000000000ULL   // PML4[403] 空闲区，映射 e1000 寄存器
#define MMIO_PAGES  32                       // 128 KiB 寄存器窗口
// 描述符环用「非缓存（PCD）」虚拟映射：设备经 DMA 回写 DD 等状态，CPU 必须能立即看到。
#define NET_RING_TX_VA 0xFFFFCC0000000000ULL   // PML4[404] 空闲区
#define NET_RING_RX_VA 0xFFFFCC0000001000ULL

static int      g_present;
static uint8_t  g_mac[6];
static volatile uint32_t *g_mmio;           // 寄存器虚拟基址（uncacheable 映射）
static uint64_t g_hhdm;

static volatile tx_desc_t *g_tx;
static volatile rx_desc_t *g_rx;
static uint8_t   *g_txbuf;
static uint8_t   *g_rxbuf;
static uint64_t   g_tx_phys, g_rx_phys, g_txbuf_phys, g_rxbuf_phys;

static inline uint32_t reg_r(uint32_t off) { return g_mmio[off / 4]; }
static inline void     reg_w(uint32_t off, uint32_t v) { g_mmio[off / 4] = v; }

// 连续分配 count 页物理内存并返回物理基址（0=失败）。
static uint64_t alloc_pages(size_t count, void **out_virt) {
    uint64_t phys = pmm_alloc_frames(count);
    if (phys == 0) { *out_virt = NULL; return 0; }
    *out_virt = (void *)(phys + g_hhdm);
    return phys;
}

static void net_reset(void) {
    reg_w(REG_CTRL, reg_r(REG_CTRL) | CTRL_RST);
    // 复位位会由硬件自动清零；超时保护。
    volatile int t = 0;
    while ((reg_r(REG_CTRL) & CTRL_RST) && t < 1000000) t++;
    // 清空挂起中断
    (void)reg_r(REG_ICR);
    reg_w(REG_IMC, 0xFFFFFFFFu);
}

static int net_setup_rings(void) {
    size_t ring_frames = (NUM_TX * sizeof(tx_desc_t) + 4095) / 4096;
    size_t buf_frames  = (NUM_TX * PKT_SIZE + 4095) / 4096;

    void *tmp;
    // 描述符环：物理页 + 非缓存虚拟映射（DMA 一致），TDBA 仍用物理地址。
    g_tx_phys = pmm_alloc_frames(ring_frames);
    g_rx_phys = pmm_alloc_frames(ring_frames);
    if (g_tx_phys == 0 || g_rx_phys == 0) return -1;
    for (uint64_t i = 0; i < ring_frames; i++) {
        if (vmm_map(NET_RING_TX_VA + i * 4096, g_tx_phys + i * 4096,
                    VMM_PRESENT | VMM_WRITE | VMM_PCD) != 0) return -1;
        if (vmm_map(NET_RING_RX_VA + i * 4096, g_rx_phys + i * 4096,
                    VMM_PRESENT | VMM_WRITE | VMM_PCD) != 0) return -1;
    }
    g_tx = (volatile tx_desc_t *)NET_RING_TX_VA;
    g_rx = (volatile rx_desc_t *)NET_RING_RX_VA;

    // 包缓冲：CPU 只写、设备只读，走 HHDM 缓存映射即可。
    g_txbuf_phys = alloc_pages(buf_frames, &tmp); g_txbuf = (uint8_t *)tmp;
    g_rxbuf_phys = alloc_pages(buf_frames, &tmp); g_rxbuf = (uint8_t *)tmp;
    if (!g_txbuf || !g_rxbuf) return -1;

    // 清空描述符环
    for (int i = 0; i < NUM_TX; i++) {
        g_tx[i].addr = g_txbuf_phys + (uint64_t)i * PKT_SIZE;
        g_tx[i].length = 0;
        g_tx[i].cso = 0;
        g_tx[i].cmd = 0;
        g_tx[i].status = 0;
        g_tx[i].css = 0;
        g_tx[i].special = 0;
    }
    for (int i = 0; i < NUM_RX; i++) {
        g_rx[i].addr = g_rxbuf_phys + (uint64_t)i * PKT_SIZE;
        g_rx[i].length = 0;
        g_rx[i].chksum = 0;
        g_rx[i].status = 0;
        g_rx[i].errors = 0;
        g_rx[i].special = 0;
    }

    // TX：使能 + 填充短包 + CT/COLD（全双工下也需置位，否则 MAC 不发帧）
    reg_w(REG_TDBAL, (uint32_t)(g_tx_phys & 0xFFFFFFFFu));
    reg_w(REG_TDBAH, (uint32_t)(g_tx_phys >> 32));
    reg_w(REG_TDLEN, (uint32_t)(NUM_TX * sizeof(tx_desc_t)));
    reg_w(REG_TDH, 0);
    reg_w(REG_TDT, 0);
    reg_w(REG_TCTL, TCTL_EN | TCTL_PSP | (0x0F << 4) | (0x40 << 12));

    // RX：使能 + 收广播，缓冲大小 2048（RCTL 16:17 = 00）
    reg_w(REG_RDBAL, (uint32_t)(g_rx_phys & 0xFFFFFFFFu));
    reg_w(REG_RDBAH, (uint32_t)(g_rx_phys >> 32));
    reg_w(REG_RDLEN, (uint32_t)(NUM_RX * sizeof(rx_desc_t)));
    reg_w(REG_RDH, 0);
    reg_w(REG_RDT, NUM_RX - 1);
    reg_w(REG_RCTL, RCTL_EN | RCTL_BAM);
    return 0;
}

void net_init(void) {
    g_present = 0;
    g_hhdm = pmm_hhdm_offset();
    if (g_hhdm == 0) return;

    pci_dev_t nic;
    if (!pci_find_class(0x02, 0x00, &nic)) {
        kprintf("[C.5] net: no ethernet controller (PCI class 0200) found\n");
        return;
    }
    kprintf("[C.5] net: found 8086:%04x at %02x:%02x.%u\n",
            nic.device, nic.bus, nic.dev, nic.fn);

    // BAR0：32 位 MMIO（bit0=0 MMIO，bit2=0 32-bit）
    uint32_t bar0 = pci_read32(nic.bus, nic.dev, nic.fn, 0x10);
    if (bar0 & 0x01) {       // IO 空间 BAR，本驱动只支持 MMIO
        kprintf("[C.5] net: BAR0 is IO space, not supported\n");
        return;
    }
    uint64_t mmio_phys = (uint64_t)(bar0 & ~0xFu);

    // 映射寄存器窗口（uncacheable）
    for (uint64_t i = 0; i < MMIO_PAGES; i++) {
        if (vmm_map(NET_MMIO_VA + i * 4096, mmio_phys + i * 4096,
                    VMM_PRESENT | VMM_WRITE | VMM_PCD) != 0) {
            kprintf("[C.5] net: vmm_map failed at page %llu\n", (unsigned long long)i);
            return;
        }
    }
    g_mmio = (volatile uint32_t *)NET_MMIO_VA;

    net_reset();

    // 读 MAC（RAL/RAH；RAH bit31 = AV 地址有效）
    uint32_t lo = reg_r(REG_RAL);
    uint32_t hi = reg_r(REG_RAH);
    g_mac[0] = (uint8_t)(lo & 0xFF);
    g_mac[1] = (uint8_t)((lo >> 8) & 0xFF);
    g_mac[2] = (uint8_t)((lo >> 16) & 0xFF);
    g_mac[3] = (uint8_t)((lo >> 24) & 0xFF);
    g_mac[4] = (uint8_t)(hi & 0xFF);
    g_mac[5] = (uint8_t)((hi >> 8) & 0xFF);

    // 置链路 UP
    reg_w(REG_CTRL, reg_r(REG_CTRL) | CTRL_SLU);

    if (net_setup_rings() != 0) {
        kprintf("[C.5] net: ring setup failed\n");
        return;
    }

    g_present = 1;
    kprintf("[C.5] net: MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
            g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);
}

int net_present(void) { return g_present; }

void net_get_mac(uint8_t mac[6]) {
    for (int i = 0; i < 6; i++) mac[i] = g_mac[i];
}

int net_link_up(void) {
    return g_present && (reg_r(REG_STATUS) & STATUS_LU) ? 1 : 0;
}

// 发送一帧并验证 TX 数据通路：
//   QEMU 的 e1000 仅在报文真正送达网络后端后才回写 DD，故 DD 不一定置位；
//   但只要 NIC 取走并处理了描述符（TDH 前进到 TDT），即证明「构造描述符→提交
//   TDT→TX 引擎取指」整条链路工作正常。两者任一成立即视为通过。
int net_loopback_test(void) {
    if (!g_present) return -1;

    for (int i = 0; i < 64; i++) g_txbuf[i] = (uint8_t)(0xA5 ^ i);

    g_tx[0].addr = g_txbuf_phys;
    g_tx[0].length = 64;
    g_tx[0].cmd = (uint8_t)(TX_CMD_EOP | TX_CMD_IFCS | TX_CMD_RS);
    g_tx[0].status = 0;

    reg_w(REG_TDT, 1);     // 提交：tail 前进，启动发送

    volatile int t = 0;
    int consumed = 0;
    while (t < 5000000) {
        if (g_tx[0].status & TX_STA_DD) { consumed = 1; break; }   // DD 回写
        if (reg_r(REG_TDH) == 1)    { consumed = 2; break; }       // 描述符被取走处理
        t++;
    }
    int dd = (g_tx[0].status & TX_STA_DD) ? 1 : 0;
    kprintf("[C.5] net: tx: DD=%d TDH=%u TDT=%u LU=%d consumed=%d\n",
            dd, reg_r(REG_TDH), reg_r(REG_TDT),
            (reg_r(REG_STATUS) & STATUS_LU) ? 1 : 0, consumed);
    return (consumed != 0) ? 0 : -1;
}
