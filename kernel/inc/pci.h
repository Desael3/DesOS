#ifndef DESOS_PCI_H
#define DESOS_PCI_H

#include <stdint.h>

// C.3 PCI 枚举（找设备）。
// 用经典配置机制 #1（0xCF8 地址端口 + 0xCFC 数据端口），兼容 QEMU/SeaBIOS 与
// 真实 PC。C.3 只枚举 bus 0..7（QEMU/SB 设备都在 bus 0；多根总线留待后续）。
// 后续接 ECAM（PCIe）或驱动具体设备时再扩展。

#define PCI_MAX_BUS  8
#define PCI_MAX_DEV  32
#define PCI_MAX_FN   8

#define PCI_VENDOR_NONE  0xFFFFu

// 配置空间访问（32 位对齐；内部按字节/半字偏移移位）
uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg);
uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg);
uint8_t  pci_read8 (uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg);
void     pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint32_t val);

void pci_init(void);   // 探测配置机制（写 0x80000000 到 0xCF8 回读 enable 位）
int  pci_ready(void);

typedef struct {
    uint8_t  bus, dev, fn;
    uint16_t vendor, device;
    uint8_t  class_base, class_sub, class_pi;  // 配置空间 0x08：基类/子类/编程接口
    uint8_t  header_type;                       // 配置空间 0x0E
} pci_dev_t;

// 枚举所有有效 BDF，最多写 maxout 个到 out，真实数量经 *count 返回
int pci_enumerate(pci_dev_t *out, int maxout, int *count);

// 按基类/子类找第一个匹配设备；找到返回 1 并填充 out
int pci_find_class(uint8_t base, uint8_t sub, pci_dev_t *out);

#endif // DESOS_PCI_H
