// gdt.c —— B.4 自有 GDT 与 TSS（对应 ProjectRoad B.4）

#include "gdt.h"
#include "kstring.h"
#include "kprintf.h"
#include <stdint.h>

struct gdtr_value {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// x86_64 的 64 位 TSS（104 字节，packed 保证偏移与手册一致）
struct tss {
    uint32_t reserved0;
    uint64_t rsp0;              // ring3 -> ring0 时使用的内核栈
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];            // ist[0] = IST1，供 #DF/#PF 使用
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed));

static uint64_t gdt[7] __attribute__((aligned(16)));
static struct tss tss __attribute__((aligned(16)));
static uint8_t rsp0_stack[16384] __attribute__((aligned(16)));
static uint8_t ist1_stack[16384] __attribute__((aligned(16)));

extern void gdt_flush(const struct gdtr_value *gdtr, uint64_t code_sel, uint64_t data_sel);
extern void tss_load(uint16_t sel);

static inline uint64_t flat_segment(uint8_t access, uint8_t granularity) {
    // base=0, limit=0xFFFFF(4K 粒度)，64 位代码段需 L=1
    return 0xFFFFULL | ((uint64_t)access << 40) | ((uint64_t)granularity << 48);
}

static void set_tss_descriptor(uint64_t base, uint32_t limit) {
    // 系统段描述符（16 字节）：低 8 字节与常规段同构，高 8 字节放 base[63:32]
    uint64_t lo = (uint64_t)(limit & 0xFFFF)
                | ((base & 0xFFFFULL) << 16)
                | (((base >> 16) & 0xFFULL) << 32)
                | (0x89ULL << 40)                       // P=1, DPL=0, type=9 (64-bit available TSS)
                | (((limit >> 16) & 0xFULL) << 48)
                | (((base >> 24) & 0xFFULL) << 56);
    gdt[5] = lo;
    gdt[6] = (base >> 32) & 0xFFFFFFFFULL;
}

static inline void read_gdtr(struct gdtr_value *out) {
    __asm__ volatile ("sgdt %0" : "=m"(*out) : : "memory");
}

static inline uint16_t read_cs(void) {
    uint16_t v;
    __asm__ volatile ("mov %%cs, %0" : "=r"(v));
    return v;
}

static inline uint16_t read_tr(void) {
    uint16_t v;
    __asm__ volatile ("str %0" : "=r"(v));
    return v;
}

void gdt_init(void) {
    kmemset(&tss, 0, sizeof(tss));
    tss.rsp0 = (uint64_t)&rsp0_stack[sizeof(rsp0_stack)];
    tss.ist[0] = (uint64_t)&ist1_stack[sizeof(ist1_stack)];
    tss.iopb_offset = (uint16_t)sizeof(tss);   // 无 I/O 许可位图

    gdt[0] = 0;                                  // null
    gdt[1] = flat_segment(0x9A, 0xAF);           // kernel code (L=1)
    gdt[2] = flat_segment(0x92, 0xCF);           // kernel data
    gdt[3] = flat_segment(0xF2, 0xCF);           // user data   (DPL=3)
    gdt[4] = flat_segment(0xFA, 0xAF);           // user code   (DPL=3, L=1)
    set_tss_descriptor((uint64_t)&tss, (uint32_t)(sizeof(tss) - 1));

    struct gdtr_value gd = { (uint16_t)(sizeof(gdt) - 1), (uint64_t)&gdt };
    gdt_flush(&gd, KERNEL_CODE_SEL, KERNEL_DATA_SEL);
    tss_load(TSS_SEL);

    kprintf("[B.4] GDT base=0x%016lx limit=%u, CS=0x%x TR=0x%x, TSS rsp0=0x%016lx IST1=0x%016lx\n",
            gdt_base(), (unsigned)gdt_limit(), (unsigned)read_cs(),
            (unsigned)read_tr(), tss.rsp0, tss.ist[0]);
}

void gdt_set_kernel_stack(uint64_t rsp0) {
    tss.rsp0 = rsp0;   // ring3 -> ring0 时 CPU 从这里取内核栈
}

uint64_t gdt_base(void) {
    struct gdtr_value v;
    read_gdtr(&v);
    return v.base;
}

uint16_t gdt_limit(void) {
    struct gdtr_value v;
    read_gdtr(&v);
    return v.limit;
}

uint16_t gdt_kernel_cs(void)       { return read_cs(); }
uint16_t gdt_task_register(void)   { return read_tr(); }
uint64_t gdt_tss_rsp0(void)        { return tss.rsp0; }
uint64_t gdt_tss_base(void)        { return (uint64_t)&tss; }
