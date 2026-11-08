#ifndef DESOS_ACPI_H
#define DESOS_ACPI_H

#include <stdint.h>

// C.7：ACPI 表解析与电源管理（对应 ProjectRoad C.7，为阶段 H 省电打基础）。
//
// 链路：Limine RSDP -> 校验 -> XSDT/RSDT -> 按签名找表（MADT/FADT/DSDT…）
//       -> FADT 取 PM1a_CNT_BLK / RESET_REG -> DSDT 扫 \_S5 包取 SLP_TYP
//       -> acpi_poweroff()/acpi_reboot()。
// 表均为 Limine 给出的 HHDM 视角可直接解引用（物理地址 + HHDM 偏移）。

// ACPI 通用表头（36 字节）
struct acpi_table_header {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

// GAS：通用地址结构（ACPI 规范 5.2.3.1，12 字节）
struct acpi_gas {
    uint8_t  space_id;      // 0=系统内存 1=系统 I/O
    uint8_t  bit_width;
    uint8_t  bit_offset;
    uint8_t  access_size;
    uint64_t address;
} __attribute__((packed));

// 初始化：校验 RSDP 并缓存根表（XSDT 优先，回退 RSDT）。
// rsdp_va 为 Limine 返回的 RSDP 虚拟地址（base revision 1 下已是 HHDM 地址）；
// 兼容传入物理地址，必要时兜底扫描 0xE0000-0xFFFFF。0=成功。
int  acpi_init(uint64_t rsdp_va, uint64_t hhdm_offset);

int  acpi_ready(void);

// 按签名找表（已校验表校验和）；找不到返回 NULL。
const struct acpi_table_header *acpi_find_table(const char *sig4);

// FADT 派生信息（C.7 电源管理用）
uint16_t acpi_fadt_sci_int(void);         // SCI 中断号
uint16_t acpi_fadt_pm1a_cnt(void);        // PM1a_CNT_BLK 端口（0=无）
const struct acpi_gas *acpi_reset_reg(void);   // RESET_REG（无效时返回 NULL）
uint8_t  acpi_reset_value(void);
uint64_t acpi_dsdt_phys(void);            // DSDT 物理基址（0=无）

// 从 DSDT 里扫 \_S5 包，取 S5 的 SLP_TYP（0-7）。0=找到。
int  acpi_s5_slp_typ(uint8_t *typ_out);

// 请求 S5 关机（PM1a_CNT |= SLP_TYP<<10 | SLP_EN）。成功则不返回（hlt 循环）；
// 无 ACPI 电源管理时返回 -1。
int  acpi_poweroff(void);

// 复位：优先 FADT RESET_REG，回退 8042（0xFE）。不返回。
void acpi_reboot(void);

#endif // DESOS_ACPI_H
