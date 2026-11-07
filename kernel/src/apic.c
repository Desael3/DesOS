// apic.c —— B.6b 中断控制器切换到 APIC（对应 ProjectRoad B.6b）
//
// 链路：Limine 的 RSDP request -> RSDT/XSDT -> MADT(APIC)
//       -> LAPIC / IOAPIC MMIO 映射 -> IOAPIC 重定向（PIT/键盘）
//       -> 屏蔽并旁路 8259A PIC -> LAPIC EOI 接管 IRQ 结束通知。
// 定时器仍复用 PIT ch0：PIT 的中断线经 IOAPIC 投递到 IDT 向量 32，
// 因此 B.7 起的调度、B.11 的键盘链路在切换后保持原有向量约定。

#include "apic.h"
#include "pmm.h"
#include "vmm.h"
#include "pic.h"
#include "kstring.h"
#include "kprintf.h"

// PML4 index 401（0xFFFFC88000000000），与堆(400)/测试(402)错开
#define APIC_MMIO_BASE   0xFFFFC88000000000ULL
#define LAPIC_VA         (APIC_MMIO_BASE)
#define IOAPIC_VA        (APIC_MMIO_BASE + 0x1000)

#define IA32_APIC_BASE_MSR 0x1B
#define APIC_BASE_ENABLE   (1ULL << 11)
#define APIC_BASE_MASK     0x0000000FFFFFF000ULL

#define LAPIC_ID        0x020
#define LAPIC_VERSION   0x030
#define LAPIC_TPR       0x080
#define LAPIC_EOI       0x0B0
#define LAPIC_SVR       0x0F0
#define LAPIC_LVT_TIMER 0x320
#define LAPIC_LVT_THERM 0x330
#define LAPIC_LVT_PERF  0x340
#define LAPIC_LVT_LINT0 0x350
#define LAPIC_LVT_LINT1 0x360
#define LAPIC_LVT_ERROR 0x370
#define LAPIC_MASKED     (1U << 16)

// 伪中断向量复用已有的 IRQ15 桩（IDT[47] 已安装），低 4 位为 1111。
#define SPURIOUS_VECTOR 0x2F

#define IOAPIC_REGSEL   0x00
#define IOAPIC_VER      0x01
#define IOAPIC_REDIR    0x10

#define IRQ_BASE_VECTOR 32
#define MAX_OVERRIDES   16

struct acpi_header {
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

struct madt_desc {
    struct acpi_header hdr;
    uint32_t lapic_addr;
    uint32_t flags;
} __attribute__((packed));

struct irq_override {
    uint8_t  bus;
    uint8_t  source;
    uint32_t gsi;
    uint16_t flags;
};

static struct irq_override overrides[MAX_OVERRIDES];
static uint32_t override_count;

static uint64_t hhdm_offset;
static uint64_t rsdp_addr;
static volatile uint32_t *lapic;
static volatile uint32_t *ioapic;
static uint64_t lapic_phys;
static uint64_t ioapic_phys;
static uint32_t madt_flags;
static uint32_t ioapic_pins;
static uint32_t lapic_version;
static uint8_t  bsp_id;
static int      enabled;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t lo = (uint32_t)val;
    uint32_t hi = (uint32_t)(val >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

static inline uint32_t lapic_read(uint32_t reg) { return lapic[reg >> 2]; }
static inline void lapic_write(uint32_t reg, uint32_t val) { lapic[reg >> 2] = val; }

// IOAPIC 只有两个 MMIO 寄存器：IOREGSEL(offset 0) 与 IOWIN(offset 0x10)。
static inline uint32_t ioapic_read(uint32_t reg) {
    ioapic[0] = reg;
    return ioapic[4];
}
static inline void ioapic_write(uint32_t reg, uint32_t val) {
    ioapic[0] = reg;
    ioapic[4] = val;
}

static uint8_t acpi_checksum(const void *p, uint64_t len) {
    const uint8_t *b = (const uint8_t *)p;
    uint8_t sum = 0;
    for (uint64_t i = 0; i < len; i++) sum = (uint8_t)(sum + b[i]);
    return sum;
}

static int sig_eq(const char *a, const char *b) {
    return kmemcmp(a, b, 4) == 0;
}

static int rsdp_valid(uint64_t va) {
    const struct rsdp_desc *r = (const struct rsdp_desc *)(uintptr_t)va;
    if (kmemcmp(r->signature, "RSD PTR ", 8) != 0) return 0;
    uint32_t len = (r->revision >= 2 && r->length >= 20) ? r->length : 20;
    if (len > 4096) return 0;
    return acpi_checksum((const void *)(uintptr_t)va, len) == 0;
}

// base revision 1 下 Limine 返回的 RSDP 是 HHDM 虚拟地址；也兼容物理地址与
// 直接扫描 0xE0000-0xFFFFF（固件没给响应时的兜底）。
static uint64_t locate_rsdp(void) {
    if (hhdm_offset == 0) return 0;
    if (rsdp_addr >= hhdm_offset && rsdp_valid(rsdp_addr)) return rsdp_addr;
    if (rsdp_addr != 0 && rsdp_addr < hhdm_offset &&
        rsdp_valid(rsdp_addr + hhdm_offset)) return rsdp_addr + hhdm_offset;
    for (uint64_t p = 0xE0000; p < 0x100000; p += 16) {
        if (rsdp_valid(p + hhdm_offset)) return p + hhdm_offset;
    }
    return 0;
}

static const struct acpi_header *find_madt(uint64_t rsdp_va) {
    const struct rsdp_desc *r = (const struct rsdp_desc *)(uintptr_t)rsdp_va;
    uint64_t tab_phys = 0;
    int xsdt = 0;
    if (r->revision >= 2 && r->xsdt_addr != 0) {
        tab_phys = r->xsdt_addr;
        xsdt = 1;
    } else if (r->rsdt_addr != 0) {
        tab_phys = r->rsdt_addr;
    }
    if (tab_phys == 0) return NULL;

    const struct acpi_header *t =
        (const struct acpi_header *)(uintptr_t)(tab_phys + hhdm_offset);
    if (t->length < sizeof(struct acpi_header) || t->length > (1U << 20)) return NULL;
    if (acpi_checksum(t, t->length) != 0) return NULL;

    uint64_t step = xsdt ? 8 : 4;
    uint64_t count = (t->length - sizeof(struct acpi_header)) / step;
    const uint8_t *p = (const uint8_t *)t + sizeof(struct acpi_header);
    for (uint64_t i = 0; i < count; i++) {
        uint64_t entry;
        if (xsdt) {
            kmemcpy(&entry, p + i * 8, 8);
        } else {
            uint32_t e32;
            kmemcpy(&e32, p + i * 4, 4);
            entry = e32;
        }
        if (entry == 0) continue;
        const struct acpi_header *h =
            (const struct acpi_header *)(uintptr_t)(entry + hhdm_offset);
        if (h->length < sizeof(struct acpi_header) || h->length > (1U << 20)) continue;
        if (sig_eq(h->signature, "APIC")) return h;
    }
    return NULL;
}

static int parse_madt(const struct acpi_header *h) {
    const struct madt_desc *m = (const struct madt_desc *)h;
    lapic_phys = m->lapic_addr;
    ioapic_phys = 0;
    madt_flags = m->flags;
    override_count = 0;

    const uint8_t *p = (const uint8_t *)h + sizeof(struct madt_desc);
    const uint8_t *end = (const uint8_t *)h + h->length;
    while (p + 2 <= end) {
        uint8_t type = p[0];
        uint8_t len = p[1];
        if (len < 2 || p + len > end) break;
        if (type == 1 && len >= 12) {                 // IOAPIC
            uint32_t addr;
            kmemcpy(&addr, p + 4, 4);
            ioapic_phys = addr;
        } else if (type == 2 && len >= 10) {          // Interrupt Source Override
            if (override_count < MAX_OVERRIDES) {
                struct irq_override *o = &overrides[override_count++];
                o->bus = p[2];
                o->source = p[3];
                kmemcpy(&o->gsi, p + 4, 4);
                kmemcpy(&o->flags, p + 8, 2);
            }
        } else if (type == 5 && len >= 12) {          // LAPIC Address Override
            kmemcpy(&lapic_phys, p + 4, 8);
        }
        p += len;
    }
    return (lapic_phys != 0 && ioapic_phys != 0) ? 0 : -1;
}

static int map_mmio(uint64_t phys, uint64_t va) {
    uint64_t page = phys & ~(uint64_t)(PAGE_SIZE - 1);
    if (page == 0) return -1;
    int rc = vmm_map(va, page, VMM_PRESENT | VMM_WRITE | VMM_PCD);
    if (rc == -4) return (vmm_get_phys(va) == page) ? 0 : -1;
    return (rc == 0) ? 0 : -1;
}

uint32_t apic_gsi_for_irq(uint8_t irq) {
    for (uint32_t i = 0; i < override_count; i++) {
        if (overrides[i].bus == 0 && overrides[i].source == irq)
            return overrides[i].gsi;
    }
    return irq;
}

static uint16_t override_flags_for_irq(uint8_t irq) {
    for (uint32_t i = 0; i < override_count; i++) {
        if (overrides[i].bus == 0 && overrides[i].source == irq)
            return overrides[i].flags;
    }
    return 0;
}

// physical destination / fixed delivery；polarity 与 trigger 按 MADT override 设置。
static void ioapic_route(uint32_t pin, uint8_t vector, uint16_t flags) {
    if (pin >= ioapic_pins) return;
    uint32_t reg = IOAPIC_REDIR + pin * 2;
    uint32_t low = vector;
    if ((flags & 3) == 3) low |= (1U << 13);            // active low
    if (((flags >> 2) & 3) == 3) low |= (1U << 15);     // level triggered
    ioapic_write(reg, low);                             // 低位不置 mask(bit16)
    ioapic_write(reg + 1, (uint32_t)bsp_id << 24);      // destination = BSP LAPIC ID
}

void apic_set_rsdp(uint64_t addr, uint64_t hhdm) {
    rsdp_addr = addr;
    hhdm_offset = hhdm;
}

int apic_init(void) {
    enabled = 0;
    int rc = 0;
    const struct acpi_header *madt;
    uint64_t apic_base_msr, msr_phys;
    uint32_t io_ver, gsi0;
    uint16_t f0;

    uint64_t rsdp_va = locate_rsdp();
    if (rsdp_va == 0) { rc = -2; goto fail; }

    madt = find_madt(rsdp_va);
    if (madt == NULL) { rc = -3; goto fail; }
    if (parse_madt(madt) != 0) { rc = -4; goto fail; }

    // 以 IA32_APIC_BASE MSR 复核基址，并打开 APIC global enable。
    apic_base_msr = rdmsr(IA32_APIC_BASE_MSR);
    msr_phys = apic_base_msr & APIC_BASE_MASK;
    if (msr_phys != 0 && msr_phys != lapic_phys) lapic_phys = msr_phys;

    if (map_mmio(lapic_phys, LAPIC_VA) != 0) { rc = -5; goto fail; }
    if (map_mmio(ioapic_phys, IOAPIC_VA) != 0) { rc = -6; goto fail; }
    lapic = (volatile uint32_t *)(uintptr_t)LAPIC_VA;
    ioapic = (volatile uint32_t *)(uintptr_t)IOAPIC_VA;

    lapic_version = lapic_read(LAPIC_VERSION);
    if ((lapic_version & 0xFF) < 0x10 || (lapic_version & 0xFF) == 0xFF) {
        rc = -7; goto fail;
    }
    bsp_id = (uint8_t)(lapic_read(LAPIC_ID) >> 24);
    io_ver = ioapic_read(IOAPIC_VER);
    ioapic_pins = ((io_ver >> 16) & 0xFF) + 1;
    if (ioapic_pins == 0 || ioapic_pins > 240) { rc = -8; goto fail; }

    wrmsr(IA32_APIC_BASE_MSR,
          (apic_base_msr & ~APIC_BASE_MASK) | (lapic_phys & APIC_BASE_MASK) | APIC_BASE_ENABLE);
    lapic_write(LAPIC_TPR, 0);
    lapic_write(LAPIC_LVT_TIMER, LAPIC_MASKED);
    lapic_write(LAPIC_LVT_THERM, LAPIC_MASKED);
    lapic_write(LAPIC_LVT_PERF,  LAPIC_MASKED);
    lapic_write(LAPIC_LVT_LINT0, LAPIC_MASKED);
    lapic_write(LAPIC_LVT_LINT1, LAPIC_MASKED);
    lapic_write(LAPIC_LVT_ERROR, LAPIC_MASKED);
    lapic_write(LAPIC_SVR, 0x100 | SPURIOUS_VECTOR);   // bit8 = APIC 软件使能
    lapic_write(LAPIC_EOI, 0);                         // 清掉可能的残留 EOI

    // IOAPIC：先屏蔽全部引脚，再只放行 PIT(IRQ0) 与键盘(IRQ1)。
    for (uint32_t pin = 0; pin < ioapic_pins; pin++) {
        uint32_t reg = IOAPIC_REDIR + pin * 2;
        ioapic_write(reg, ioapic_read(reg) | (1U << 16));
    }
    gsi0 = apic_gsi_for_irq(0);
    f0 = override_flags_for_irq(0);
    ioapic_route(0, IRQ_BASE_VECTOR, 0);               // 无 override 时的 PIT 连线
    if (gsi0 != 0) ioapic_route(gsi0, IRQ_BASE_VECTOR, f0);
    ioapic_route(apic_gsi_for_irq(1), IRQ_BASE_VECTOR + 1, override_flags_for_irq(1));

    // 屏蔽 8259A；PCAT_COMPAT 平台上经 IMCR 把 INTR 从 PIC 切到 APIC。
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
    if (madt_flags & 1) {
        outb(0x22, 0x70);
        outb(0x23, 0x01);
    }

    enabled = 1;
    kprintf("[B.6b] MADT: lapic=0x%016lx ioapic=0x%016lx bsp=%u ver=0x%x "
            "pins=%u overrides=%u flags=0x%x gsi0=%u, PIC disabled\n",
            lapic_phys, ioapic_phys, (unsigned)bsp_id, (unsigned)(lapic_version & 0xFF),
            (unsigned)ioapic_pins, (unsigned)override_count,
            (unsigned)madt_flags, (unsigned)gsi0);
    return 0;

fail:
    kprintf("[B.6b] apic_init failed rc=%d (rsdp=0x%016lx hhdm=0x%016lx)\n",
            rc, rsdp_addr, hhdm_offset);
    return rc;
}

void apic_send_eoi(void) {
    if (enabled && lapic != NULL) lapic_write(LAPIC_EOI, 0);
}

void apic_fallback_to_pic(void) {
    if (ioapic != NULL) {
        for (uint32_t pin = 0; pin < ioapic_pins; pin++) {
            uint32_t reg = IOAPIC_REDIR + pin * 2;
            ioapic_write(reg, ioapic_read(reg) | (1U << 16));
        }
    }
    if (lapic != NULL) lapic_write(LAPIC_SVR, SPURIOUS_VECTOR);
    if (madt_flags & 1) {
        outb(0x22, 0x70);
        outb(0x23, 0x00);
    }
    enabled = 0;
    pic_init();
    pic_clear_mask(0);
    kprintf("[B.6b] fallback: 8259A PIC restored\n");
}

int      apic_enabled(void)         { return enabled; }
uint64_t apic_lapic_phys(void)       { return lapic_phys; }
uint64_t apic_ioapic_phys(void)      { return ioapic_phys; }
uint8_t  apic_bsp_id(void)           { return bsp_id; }
uint32_t apic_lapic_version(void)   { return lapic_version; }
uint32_t apic_ioapic_pins(void)      { return ioapic_pins; }
uint32_t apic_madt_flags(void)       { return madt_flags; }
uint32_t apic_override_count(void)  { return override_count; }

uint32_t apic_redir_low(uint32_t pin) {
    if (ioapic == NULL || pin >= ioapic_pins) return 0;
    return ioapic_read(IOAPIC_REDIR + pin * 2);
}
