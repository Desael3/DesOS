// apic.c —— B.6b 中断控制器切换到 APIC（对应 ProjectRoad B.6b）
//
// 链路：Limine 的 RSDP request -> RSDT/XSDT -> MADT(APIC)
//       -> LAPIC / IOAPIC MMIO 映射 -> IOAPIC 重定向（PIT/键盘）
//       -> 屏蔽并旁路 8259A PIC -> LAPIC EOI 接管 IRQ 结束通知。
// 定时器仍复用 PIT ch0：PIT 的中断线经 IOAPIC 投递到 IDT 向量 32，
// 因此 B.7 起的调度、B.11 的键盘链路在切换后保持原有向量约定。

#include "apic.h"
#include "acpi.h"
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

// 表头 / RSDP 结构与 RSDP->XSDT/RSDT 查找已统一迁到 acpi.c（C.7），
// 这里只保留 MADT 自身的布局与解析。
struct madt_desc {
    struct acpi_table_header hdr;
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

// RSDP 校验与根表查找（原 locate_rsdp/find_madt）已由 acpi.c（C.7）统一提供，
// apic_init 直接用 acpi_find_table("APIC") 拿 MADT。

static int parse_madt(const struct acpi_table_header *h) {
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

int apic_init(void) {
    enabled = 0;
    int rc = 0;
    const struct acpi_table_header *madt;
    uint64_t apic_base_msr, msr_phys;
    uint32_t io_ver, gsi0;
    uint16_t f0;

    // RSDP/根表已由 acpi_init()（C.7，kernel_main 在堆初始化后调用）解析好；
    // 若 ACPI 未就绪则 MADT 找不到，走 PIC 回退路径。
    madt = acpi_find_table("APIC");
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

    // IOAPIC：先屏蔽全部引脚，再放行 PIT(IRQ0)、键盘(IRQ1) 与 C.1c 鼠标(IRQ12)。
    for (uint32_t pin = 0; pin < ioapic_pins; pin++) {
        uint32_t reg = IOAPIC_REDIR + pin * 2;
        ioapic_write(reg, ioapic_read(reg) | (1U << 16));
    }
    gsi0 = apic_gsi_for_irq(0);
    f0 = override_flags_for_irq(0);
    ioapic_route(0, IRQ_BASE_VECTOR, 0);               // 无 override 时的 PIT 连线
    if (gsi0 != 0) ioapic_route(gsi0, IRQ_BASE_VECTOR, f0);
    ioapic_route(apic_gsi_for_irq(1), IRQ_BASE_VECTOR + 1, override_flags_for_irq(1));
    // C.1c：PS/2 鼠标 IRQ12 -> 向量 44，与键盘一起随 APIC 初始化放行
    ioapic_route(apic_gsi_for_irq(12), IRQ_BASE_VECTOR + 12, override_flags_for_irq(12));

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
    kprintf("[B.6b] apic_init failed rc=%d (acpi_ready=%d)\n",
            rc, acpi_ready());
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

// 屏蔽/放开单个 IOAPIC 引脚（redir bit16）。自检注入键盘事件时用，
// 防止真实键盘 IRQ 与 kbd_inject_scancode 竞争状态机（VMware 实测踩坑）。
void apic_mask_gsi(uint32_t gsi, int masked) {
    if (!enabled || ioapic == NULL || gsi >= ioapic_pins) return;
    uint32_t reg = IOAPIC_REDIR + gsi * 2;
    uint32_t low = ioapic_read(reg);
    low = masked ? (low | (1U << 16)) : (low & ~(1U << 16));
    ioapic_write(reg, low);
}
