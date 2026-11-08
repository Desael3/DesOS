// pci.c —— C.3 PCI 枚举（经典配置机制 #1）
//
// 通过 I/O 端口 0xCF8（地址）/ 0xCFC（数据）访问 PCI 配置空间：
//   地址字 = 0x80000000 | bus<<16 | dev<<11 | fn<<8 | (reg & 0xFC)
// bit31 为 enable，reg 必须 4 字节对齐；16/8 位访问在读出的双字内移位取。
// 枚举时先读 function 0 的 Vendor ID，0xFFFF 表示无设备；header type 的
// bit7 标记多 function，否则只扫 function 0。
//
// 注：inl/outl 仅在当前单元使用，保持 static inline 与 serial/pic 等同风格。

#include "pci.h"

#define PCI_CONFIG_ADDR 0xCF8u
#define PCI_CONFIG_DATA 0xCFCu

static inline uint32_t inl(uint16_t port) {
    uint32_t v;
    __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static int g_pci_ready;

static uint32_t cfg_addr(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    return 0x80000000u
         | ((uint32_t)bus  << 16)
         | ((uint32_t)dev  << 11)
         | ((uint32_t)fn   << 8)
         | ((uint32_t)(reg & 0xFC));
}

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    if (!g_pci_ready) return 0xFFFFFFFFu;
    outl(PCI_CONFIG_ADDR, cfg_addr(bus, dev, fn, reg));
    return inl(PCI_CONFIG_DATA);
}
uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    uint32_t v = pci_read32(bus, dev, fn, reg & 0xFC);
    return (uint16_t)((v >> ((reg & 2) * 8)) & 0xFFFFu);
}
uint8_t pci_read8(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    uint32_t v = pci_read32(bus, dev, fn, reg & 0xFC);
    return (uint8_t)((v >> ((reg & 3) * 8)) & 0xFFu);
}
void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint32_t val) {
    if (!g_pci_ready) return;
    outl(PCI_CONFIG_ADDR, cfg_addr(bus, dev, fn, reg));
    outl(PCI_CONFIG_DATA, val);
}

void pci_init(void) {
    // 探测配置机制：写 0x80000000 到 0xCF8，回读应保留 enable 位。
    outl(PCI_CONFIG_ADDR, 0x80000000u);
    g_pci_ready = (inl(PCI_CONFIG_ADDR) & 0x80000000u) ? 1 : 0;
}

int pci_ready(void) { return g_pci_ready; }

int pci_enumerate(pci_dev_t *out, int maxout, int *count) {
    int n = 0;
    if (!g_pci_ready) { if (count) *count = 0; return 0; }

    for (uint8_t bus = 0; bus < PCI_MAX_BUS; bus++) {
        for (uint8_t dev = 0; dev < PCI_MAX_DEV; dev++) {
            uint32_t id = pci_read32(bus, dev, 0, 0);
            if ((uint16_t)(id & 0xFFFFu) == PCI_VENDOR_NONE) continue;  // 无设备
            uint8_t hdr = pci_read8(bus, dev, 0, 0x0E);
            int multifn = hdr & 0x80;

            for (uint8_t fn = 0; fn < PCI_MAX_FN; fn++) {
                uint32_t vid = pci_read32(bus, dev, fn, 0);
                if ((uint16_t)(vid & 0xFFFFu) == PCI_VENDOR_NONE) {
                    if (!multifn) break; else continue;
                }
                if (n < maxout) {
                    uint32_t cls = pci_read32(bus, dev, fn, 0x08);
                    out[n].bus = bus; out[n].dev = dev; out[n].fn = fn;
                    out[n].vendor = (uint16_t)(vid & 0xFFFFu);
                    out[n].device = (uint16_t)(vid >> 16);
                    // 配置空间 0x08: 0x08=Revision, 0x09=ProgIF,
                    // 0x0A=Sub-Class, 0x0B=Base Class (小端 32 位)
                    out[n].class_pi    = (uint8_t)((cls >> 8)  & 0xFFu);
                    out[n].class_sub   = (uint8_t)((cls >> 16) & 0xFFu);
                    out[n].class_base  = (uint8_t)((cls >> 24) & 0xFFu);
                    out[n].header_type = pci_read8(bus, dev, fn, 0x0E);
                }
                n++;
                if (!multifn && fn == 0) break;
            }
        }
    }
    if (count) *count = n;
    return n;
}

int pci_find_class(uint8_t base, uint8_t sub, pci_dev_t *out) {
    pci_dev_t buf[64];
    int n, got = pci_enumerate(buf, 64, &n);
    for (int i = 0; i < got; i++) {
        if (buf[i].class_base == base && buf[i].class_sub == sub) {
            if (out) *out = buf[i];
            return 1;
        }
    }
    return 0;
}
