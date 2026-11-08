// acpi.c —— C.7 ACPI 表解析与电源管理（对应 ProjectRoad C.7）
//
// B.6b 已在 apic.c 内做过 RSDP -> XSDT -> MADT 的解析；本模块把这段能力
// 抽成公共实现并扩展到 FADT/DSDT，供 APIC、电源管理与将来的省电（阶段 H）共用。
//
// 关键点（ACPI 规范）：
//   - RSDP：签名 "RSD PTR "，revision>=2 校验 36 字节 extended checksum。
//   - XSDT/RSDT 项为指向各表「物理地址」的指针，统一加 HHDM 偏移访问。
//   - 每张表自身带校验和（整表字节和 == 0）。
//   - FADT（"FACP"）：PM1a_CNT_BLK 在偏移 64，SMI_CMD 48，ACPI_ENABLE 52；
//     ACPI 2.0+ 的 X_ 64 位字段优先（X_PM1a_CNT 偏移 180、X_DSDT 偏移 156）；
//     RESET_REG（GAS）在偏移 128、RESET_VALUE 在偏移 140。
//   - S5：往 PM1a_CNT 写 (SLP_TYP << 10) | SLP_EN(bit13)。SLP_TYP 从 DSDT 的
//     \_S5 包解析（AML：Name(\_S5, Package(n){BytePrefix val, ...})）。
//
// 注：访问表全部走 HHDM 虚拟地址，不需要 vmm_map（Limine 已映射全部 RAM）。

#include "acpi.h"
#include "kstring.h"
#include "kprintf.h"

struct rsdp_desc {
    char     signature[8];
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_addr;
    uint32_t length;
    uint64_t xsdt_addr;
    uint8_t  extended_checksum;
    uint8_t  reserved[3];
} __attribute__((packed));

#define MAX_TABLES 24

static struct {
    const struct acpi_table_header *tables[MAX_TABLES];
    uint32_t table_count;
    char     oem_id[7];
    uint16_t sci_int;
    uint16_t pm1a_cnt;
    uint16_t pm1b_cnt;
    uint32_t smi_cmd;
    uint8_t  acpi_enable;
    uint8_t  acpi_disable;
    struct acpi_gas reset_reg;      // space_id==0xFF 表示无效
    uint8_t  reset_value;
    uint64_t dsdt_phys;
    uint8_t  s5_slp_typ;
    int      s5_found;
    int      ready;
} acpi;

static uint64_t hhdm;
static uint64_t rsdp_va;

static inline uint64_t p2v(uint64_t phys) { return phys + hhdm; }

static uint8_t checksum(const void *p, uint64_t len) {
    const uint8_t *b = (const uint8_t *)p;
    uint8_t sum = 0;
    for (uint64_t i = 0; i < len; i++) sum = (uint8_t)(sum + b[i]);
    return sum;
}

static int rsdp_ok(uint64_t va) {
    const struct rsdp_desc *r = (const struct rsdp_desc *)(uintptr_t)va;
    if (kmemcmp(r->signature, "RSD PTR ", 8) != 0) return 0;
    uint32_t len = (r->revision >= 2 && r->length >= 20 && r->length <= 4096)
                       ? r->length : 20;
    if (checksum((const void *)(uintptr_t)va, len) != 0) return 0;
    if (r->revision < 2) return 1;                    // ACPI 1.0：只校验前 20 字节
    return checksum((const void *)(uintptr_t)va, r->length) == 0;
}

// Limine 给 HHDM 地址；兼容物理地址；最后兜底扫 BIOS 区。
static uint64_t locate_rsdp(uint64_t va) {
    if (va != 0 && va >= hhdm && rsdp_ok(va)) return va;
    if (va != 0 && va < hhdm && va + hhdm > hhdm && rsdp_ok(va + hhdm)) return va + hhdm;
    for (uint64_t p = 0xE0000; p < 0x100000; p += 16) {
        if (rsdp_ok(p + hhdm)) return p + hhdm;
    }
    return 0;
}

// 把根表（XSDT/RSDT）的所有条目缓存进 acpi.tables（逐表校验）。
static int load_root_table(void) {
    const struct rsdp_desc *r = (const struct rsdp_desc *)(uintptr_t)rsdp_va;
    uint64_t root_phys;
    int xsdt = 0;
    if (r->revision >= 2 && r->xsdt_addr != 0) {
        root_phys = r->xsdt_addr;
        xsdt = 1;
    } else if (r->rsdt_addr != 0) {
        root_phys = r->rsdt_addr;
    } else {
        return -1;
    }

    const struct acpi_table_header *root =
        (const struct acpi_table_header *)(uintptr_t)p2v(root_phys);
    if (root->length < sizeof(struct acpi_table_header) || root->length > (1U << 20))
        return -1;
    if (checksum(root, root->length) != 0) return -1;

    uint64_t step = xsdt ? 8 : 4;
    uint64_t count = (root->length - sizeof(struct acpi_table_header)) / step;
    const uint8_t *entry = (const uint8_t *)root + sizeof(struct acpi_table_header);
    for (uint64_t i = 0; i < count; i++) {
        uint64_t tab_phys;
        if (xsdt) {
            kmemcpy(&tab_phys, entry + i * 8, 8);
        } else {
            uint32_t e32;
            kmemcpy(&e32, entry + i * 4, 4);
            tab_phys = e32;
        }
        if (tab_phys == 0) continue;
        const struct acpi_table_header *h =
            (const struct acpi_table_header *)(uintptr_t)p2v(tab_phys);
        if (h->length < sizeof(struct acpi_table_header) || h->length > (1U << 20))
            continue;
        if (checksum(h, h->length) != 0) continue;
        if (acpi.table_count < MAX_TABLES)
            acpi.tables[acpi.table_count++] = h;
    }
    return acpi.table_count > 0 ? 0 : -1;
}

// FADT（"FACP"）里取电源管理寄存器块。
static void parse_fadt(const struct acpi_table_header *fadt) {
    const uint8_t *p = (const uint8_t *)fadt;
    uint32_t dsdt32 = 0;
    kmemcpy(&dsdt32, p + 40, 4);
    acpi.dsdt_phys = dsdt32;

    kmemcpy(&acpi.sci_int,  p + 46, 2);
    kmemcpy(&acpi.smi_cmd,  p + 48, 4);
    kmemcpy(&acpi.acpi_enable,  p + 52, 1);
    kmemcpy(&acpi.acpi_disable, p + 53, 1);
    uint32_t pm1a32 = 0, pm1b32 = 0;
    kmemcpy(&pm1a32, p + 64, 4);
    kmemcpy(&pm1b32, p + 68, 4);
    uint8_t pm1_cnt_len = 0;
    kmemcpy(&pm1_cnt_len, p + 89, 1);

    // ACPI 2.0+：优先 64 位 X_ 字段
    if (fadt->revision >= 2) {
        uint64_t x_dsdt = 0, x_pm1a = 0, x_pm1b = 0;
        kmemcpy(&x_dsdt,  p + 156, 8);
        kmemcpy(&x_pm1a,  p + 180, 8);
        kmemcpy(&x_pm1b,  p + 188, 8);
        if (x_dsdt != 0) acpi.dsdt_phys = x_dsdt;
        if (x_pm1a != 0) pm1a32 = (uint32_t)x_pm1a;
        if (x_pm1b != 0) pm1b32 = (uint32_t)x_pm1b;

        struct acpi_gas rr;
        kmemcpy(&rr, p + 128, sizeof(rr));           // RESET_REG（ACPI 2.0+）
        if ((rr.space_id == 0 || rr.space_id == 1) && rr.address != 0 &&
            rr.bit_width == 8) {
            acpi.reset_reg = rr;
            kmemcpy(&acpi.reset_value, p + 140, 1);
        }
    }
    // 仅当端口宽度合理才记录（QEMU/常见 PC：PM1a_CNT=0x604，长度 2）
    if (pm1a32 != 0 && pm1a32 < 0x10000) {
        acpi.pm1a_cnt = (uint16_t)pm1a32;
        if (pm1b32 != 0 && pm1b32 < 0x10000) acpi.pm1b_cnt = (uint16_t)pm1b32;
    }
}

// DSDT 里扫 \_S5 包取 SLP_TYP。AML：Name(\_S5, Package(n){BytePrefix val,...})。
// 只做最小解析：定位 "_S5_" -> PackageOp(0x12) -> PkgLength -> 首元素。
static void scan_dsdt_s5(const struct acpi_table_header *dsdt) {
    const uint8_t *p = (const uint8_t *)dsdt;
    uint32_t len = dsdt->length;
    for (uint32_t i = 0; i + 4 < len; i++) {
        if (!(p[i] == '_' && p[i + 1] == 'S' && p[i + 2] == '5' && p[i + 3] == '_'))
            continue;
        // 名字后紧跟 PackageOp(0x12)（允许跳过少量前缀字节）
        for (uint32_t j = i + 4; j < i + 8 && j < len; j++) {
            if (p[j] != 0x12) continue;
            if (j + 2 >= len) break;
            uint8_t b0 = p[j + 1];                   // PkgLength 首字节
            uint32_t extra = b0 >> 6;                // 长度编码的附加字节数
            uint32_t q = j + 2 + extra;              // NumElements
            if (q >= len) break;
            q++;                                     // 跳过 NumElements，到首元素
            if (q >= len) break;
            if (p[q] == 0x0A && q + 1 < len && p[q + 1] < 8) {   // BytePrefix
                acpi.s5_slp_typ = p[q + 1];
                acpi.s5_found = 1;
                return;
            }
            if (p[q] == 0x00) { acpi.s5_slp_typ = 0; acpi.s5_found = 1; return; }  // ZeroOp
            if (p[q] == 0x01) { acpi.s5_slp_typ = 1; acpi.s5_found = 1; return; }  // OneOp
            break;                                   // 其它编码不支持，走默认值
        }
    }
}

int acpi_init(uint64_t va, uint64_t hhdm_offset) {
    acpi.ready = 0;
    acpi.table_count = 0;
    acpi.pm1a_cnt = 0;
    acpi.pm1b_cnt = 0;
    acpi.dsdt_phys = 0;
    acpi.s5_found = 0;
    acpi.s5_slp_typ = 0;
    acpi.reset_reg.space_id = 0xFF;
    acpi.reset_reg.address = 0;
    hhdm = hhdm_offset;
    if (hhdm == 0) return -1;

    rsdp_va = locate_rsdp(va);
    if (rsdp_va == 0) { kprintf("[C.7] acpi: RSDP not found\n"); return -2; }

    const struct rsdp_desc *r = (const struct rsdp_desc *)(uintptr_t)rsdp_va;
    kmemcpy(acpi.oem_id, r->oem_id, 6);
    acpi.oem_id[6] = '\0';

    if (load_root_table() != 0) {
        kprintf("[C.7] acpi: root table (XSDT/RSDT) invalid\n");
        return -3;
    }
    acpi.ready = 1;                              // 先就绪，find_table 才能查表

    const struct acpi_table_header *fadt = acpi_find_table("FACP");
    if (fadt != NULL) {
        parse_fadt(fadt);
        const struct acpi_table_header *dsdt =
            (const struct acpi_table_header *)(uintptr_t)p2v(acpi.dsdt_phys);
        if (acpi.dsdt_phys != 0 && dsdt->length >= 44 &&
            dsdt->length <= (1U << 20) &&
            kmemcmp(dsdt->signature, "DSDT", 4) == 0 &&
            checksum(dsdt, dsdt->length) == 0) {
            scan_dsdt_s5(dsdt);
        } else {
            acpi.dsdt_phys = 0;
        }
    }

    kprintf("[C.7] acpi: rsdp rev=%u oem=%s tables=%u facp=%s "
            "sci=%u pm1a=0x%x dsdt=%s s5=%s",
            (unsigned)r->revision, acpi.oem_id, (unsigned)acpi.table_count,
            fadt != NULL ? "yes" : "no",
            (unsigned)acpi.sci_int, (unsigned)acpi.pm1a_cnt,
            acpi.dsdt_phys != 0 ? "yes" : "no",
            acpi.s5_found ? "found" : "default");
    if (acpi.s5_found) kprintf(" (typ=%u)", (unsigned)acpi.s5_slp_typ);
    if (acpi.reset_reg.space_id != 0xFF)
        kprintf(" reset=0x%x/0x%02x", (unsigned)acpi.reset_reg.address,
                (unsigned)acpi.reset_value);
    kprintf("\n");
    return 0;
}

int acpi_ready(void) { return acpi.ready; }

const struct acpi_table_header *acpi_find_table(const char *sig4) {
    if (!acpi.ready || sig4 == 0) return 0;
    for (uint32_t i = 0; i < acpi.table_count; i++) {
        if (kmemcmp(acpi.tables[i]->signature, sig4, 4) == 0) return acpi.tables[i];
    }
    return 0;
}

uint16_t acpi_fadt_sci_int(void)  { return acpi.sci_int; }
uint16_t acpi_fadt_pm1a_cnt(void) { return acpi.pm1a_cnt; }
uint64_t acpi_dsdt_phys(void)     { return acpi.dsdt_phys; }

const struct acpi_gas *acpi_reset_reg(void) {
    return acpi.reset_reg.space_id != 0xFF ? &acpi.reset_reg : 0;
}

uint8_t acpi_reset_value(void) { return acpi.reset_value; }

int acpi_s5_slp_typ(uint8_t *typ_out) {
    if (!acpi.s5_found || typ_out == 0) return -1;
    *typ_out = acpi.s5_slp_typ;
    return 0;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

// S5 关机：PM1a_CNT[SLP_TYP:10-12] = \_S5 值，SLP_EN(bit13)=1。
// QEMU（PIIX4/ICH9，SeaBIOS 已开 SCI_EN）在 SLP_EN 置位时请求关机。
int acpi_poweroff(void) {
    if (!acpi.ready || acpi.pm1a_cnt == 0) return -1;
    if (acpi.smi_cmd != 0 && acpi.acpi_enable != 0)
        outb((uint16_t)acpi.smi_cmd, acpi.acpi_enable);   // 确保 SCI_EN（重复写无害）

    uint16_t val = (uint16_t)(((acpi.s5_found ? acpi.s5_slp_typ : 0) << 10) | (1u << 13));
    outw(acpi.pm1a_cnt, val);
    if (acpi.pm1b_cnt != 0) outw(acpi.pm1b_cnt, val);

    for (;;) __asm__ volatile ("cli; hlt");    // 正常情况下 QEMU/硬件直接断电
}

// 复位：优先 FADT RESET_REG（QEMU：0xCF9 写 0x06），回退 8042（0xFE）。
void acpi_reboot(void) {
    if (acpi.reset_reg.space_id == 1 && acpi.reset_reg.address != 0 &&
        acpi.reset_reg.address < 0x10000) {
        outb((uint16_t)acpi.reset_reg.address, acpi.reset_value);
    }
    for (int i = 0; i < 100; i++) inb(0x64);   // 排空 8042
    outb(0x64, 0xFE);                          // 8042 脉冲复位
    for (;;) __asm__ volatile ("cli; hlt");
}
