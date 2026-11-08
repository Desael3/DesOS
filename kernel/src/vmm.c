// vmm.c —— B.2 虚拟内存：4 级页表管理（对应 ProjectRoad B.2）
//
// 接管 Limine 建立的 PML4（CR3），通过 HHDM 直接读写页表页。
// 注意：页表页本身也是物理内存，因此 (phys + hhdm) 一定可访问。

#include "vmm.h"
#include "pmm.h"
#include "kstring.h"
#include "kprintf.h"

#define ADDR_MASK 0x000FFFFFFFFFF000ULL

static uint64_t hhdm;
static uint64_t pml4_phys;

static inline uint64_t read_cr3(void) {
    uint64_t v;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(v));
    return v;
}

static inline void invlpg(uint64_t va) {
    __asm__ volatile ("invlpg (%0)" : : "r"(va) : "memory");
}

static inline uint64_t *table(uint64_t phys) {
    return (uint64_t *)(phys + hhdm);
}

static inline uint64_t idx_of(uint64_t va, int level) {
    return (va >> (39 - 9 * level)) & 511ULL;
}

// 走到 va 对应的 PT（页表）；create=1 时按需创建中间级表。
// 若上层是大页（无法安全拆分），返回 NULL 而不是覆盖。
static uint64_t *walk_pt_in(uint64_t pml4, uint64_t va, int create) {
    uint64_t *t = table(pml4);
    for (int level = 0; level < 3; level++) {
        uint64_t i = idx_of(va, level);
        uint64_t e = t[i];
        if (!(e & VMM_PRESENT)) {
            if (!create) return NULL;
            uint64_t p = pmm_alloc_frame();
            if (p == 0) return NULL;
            kmemset(table(p), 0, (size_t)PAGE_SIZE);
            // 中间级带 USER 位，将来用户态映射时不会被上层拦住；
            // 最终是否允许 ring3 访问由 PTE 决定。
            t[i] = (p & ADDR_MASK) | VMM_PRESENT | VMM_WRITE | VMM_USER;
            e = t[i];
        }
        if (e & VMM_PS) return NULL;
        t = table(e & ADDR_MASK);
    }
    return t;
}

static uint64_t *walk_pt(uint64_t va, int create) {
    return walk_pt_in(pml4_phys, va, create);
}

uint64_t vmm_create_user_pml4(void) {
    if (hhdm == 0) return 0;
    uint64_t p = pmm_alloc_frame();
    if (p == 0) return 0;
    uint64_t *dst = table(p);
    uint64_t *src = table(pml4_phys);
    kmemset(dst, 0, (size_t)PAGE_SIZE);
    // 复制 PML4 更高半（index 256-511）：内核镜像、HHDM、内核堆等。
    // 这些项的 U/S=0，ring3 依然访问不到内核页。
    for (int i = 256; i < 512; i++) dst[i] = src[i];
    return p;
}

int vmm_map_user(uint64_t pml4, uint64_t va, uint64_t phys, uint64_t flags) {
    if (hhdm == 0 || pml4 == 0) return -1;
    if ((va & (PAGE_SIZE - 1)) || (phys & (PAGE_SIZE - 1))) return -2;
    uint64_t *pt = walk_pt_in(pml4, va, 1);
    if (pt == NULL) return -3;
    uint64_t i = idx_of(va, 3);
    if (pt[i] & VMM_PRESENT) return -4;
    pt[i] = (phys & ADDR_MASK)
          | (flags & (VMM_WRITE | VMM_USER | VMM_NX)) | VMM_PRESENT;
    return 0;
}

// D.2c：只读查询用户地址空间某 4K 对齐 VA 的 PTE 原始值（0=未映射/无法到达）。
// 供 B.10 自检验证 W^X 映射（text NX=0 且 W=0、栈 NX=1 且 W=1）。
uint64_t vmm_get_pte_user(uint64_t pml4, uint64_t va) {
    if (hhdm == 0 || pml4 == 0 || (va & (PAGE_SIZE - 1))) return 0;
    const uint64_t *pt = walk_pt_in(pml4, va, 0);
    if (pt == NULL) return 0;
    return pt[idx_of(va, 3)];
}

// D.2c：确保 EFER.NXE（MSR 0xC0000080 bit11）开启——PTE 的 NX 位只有在
// NXE=1 时才被 CPU 尊重。Limine 通常已开启，这里兜底自检式启用。
static void vmm_enable_nxe(void) {
    uint32_t msr = 0xC0000080u;
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    if (!(lo & (1u << 11))) {
        lo |= 1u << 11;
        __asm__ volatile ("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
        kprintf("[D.2c] EFER.NXE enabled\n");
    }
}

void vmm_init(void) {
    hhdm = pmm_hhdm_offset();
    pml4_phys = read_cr3() & ADDR_MASK;
    if (hhdm == 0 || pml4_phys == 0) {
        kprintf("[B.2] FATAL: no HHDM / invalid CR3\n");
        hhdm = 0;
        return;
    }
    vmm_enable_nxe();

    uint64_t *pml4 = table(pml4_phys);
    uint64_t i_heap = idx_of(KHEAP_BASE, 0);
    uint64_t i_test = idx_of(VMM_TEST_VA, 0);
    kprintf("[B.2] cr3=0x%016lx pml4[%lu]=0x%016lx pml4[%lu]=0x%016lx\n",
            pml4_phys, i_heap, pml4[i_heap], i_test, pml4[i_test]);
    if (pml4[i_heap] & VMM_PRESENT)
        kprintf("[B.2] WARN: heap PML4 slot already mapped\n");
    if (pml4[i_test] & VMM_PRESENT)
        kprintf("[B.2] WARN: test PML4 slot already mapped\n");

    kprintf("[B.2] kernel va 0x%016lx -> phys 0x%016lx\n",
            (uint64_t)(uintptr_t)&vmm_init,
            vmm_get_phys((uint64_t)(uintptr_t)&vmm_init));
}

int vmm_map(uint64_t va, uint64_t phys, uint64_t flags) {
    if (hhdm == 0) return -1;
    if ((va & (PAGE_SIZE - 1)) || (phys & (PAGE_SIZE - 1))) return -2;
    uint64_t *pt = walk_pt(va, 1);
    if (pt == NULL) return -3;
    uint64_t i = idx_of(va, 3);
    if (pt[i] & VMM_PRESENT) return -4;
    pt[i] = (phys & ADDR_MASK)
          | (flags & (VMM_WRITE | VMM_USER | VMM_NX | VMM_PCD)) | VMM_PRESENT;
    invlpg(va);
    return 0;
}

int vmm_unmap(uint64_t va) {
    if (hhdm == 0) return -1;
    uint64_t *pt = walk_pt(va, 0);
    if (pt == NULL) return -2;
    uint64_t i = idx_of(va, 3);
    if (!(pt[i] & VMM_PRESENT)) return -3;
    pt[i] = 0;
    invlpg(va);
    // 暂不回收空页表页：留待后续实现引用计数，避免误释放仍在用的表。
    return 0;
}

uint64_t vmm_get_phys(uint64_t va) {
    if (hhdm == 0) return 0;
    uint64_t *pml4 = table(pml4_phys);
    uint64_t e = pml4[idx_of(va, 0)];
    if (!(e & VMM_PRESENT)) return 0;

    uint64_t *pdpt = table(e & ADDR_MASK);
    e = pdpt[idx_of(va, 1)];
    if (!(e & VMM_PRESENT)) return 0;
    if (e & VMM_PS)   // 1 GiB 大页
        return (e & ADDR_MASK & ~((1ULL << 30) - 1)) + (va & ((1ULL << 30) - 1));

    uint64_t *pd = table(e & ADDR_MASK);
    e = pd[idx_of(va, 2)];
    if (!(e & VMM_PRESENT)) return 0;
    if (e & VMM_PS)   // 2 MiB 大页
        return (e & ADDR_MASK & ~((1ULL << 21) - 1)) + (va & ((1ULL << 21) - 1));

    uint64_t *pt = table(e & ADDR_MASK);
    e = pt[idx_of(va, 3)];
    if (!(e & VMM_PRESENT)) return 0;
    return (e & ADDR_MASK) + (va & (PAGE_SIZE - 1));
}

uint64_t vmm_current_pml4_phys(void) { return pml4_phys; }
