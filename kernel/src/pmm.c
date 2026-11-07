// pmm.c —— B.1 物理内存管理：页帧分配器（bitmap）
// 对应 ProjectRoad B.1。数据源：Limine MEMMAP 请求 + HHDM 请求。

#include "pmm.h"
#include "kstring.h"
#include "kprintf.h"

static uint8_t  *bitmap;          // 经 HHDM 映射后的 bitmap 虚拟地址
static uint64_t  bitmap_bytes;    // bitmap 字节数（页对齐）
static uint64_t  max_pfn;         // bitmap 管理的页帧总数（pfn = phys / PAGE_SIZE）
static uint64_t  hhdm;            // HHDM 偏移
static uint64_t  total_pages;     // usable 页数
static uint64_t  free_pages;      // 当前空闲页数
static uint64_t  hint_pfn;        // 单页分配的扫描起点

static inline uint8_t bit_of(uint64_t pfn) {
    return (uint8_t)(1u << (pfn & 7));
}

static inline int is_free(uint64_t pfn) {
    return (bitmap[pfn >> 3] & bit_of(pfn)) == 0;
}

static inline void set_used(uint64_t pfn) {
    bitmap[pfn >> 3] |= bit_of(pfn);
}

static inline void set_free(uint64_t pfn) {
    bitmap[pfn >> 3] &= (uint8_t)~bit_of(pfn);
}

// 把某页标为「不可分配」，只在实际从空闲变已用时扣减计数。
static void reserve_pfn(uint64_t pfn) {
    if (pfn >= max_pfn) return;
    if (is_free(pfn)) {
        set_used(pfn);
        free_pages--;
    }
}

// 在 usable 区间里找一块能放下 bitmap 的连续空间（页对齐起点）。
static uint64_t find_bitmap_region(struct limine_memmap_response *mm, uint64_t bytes) {
    for (uint64_t i = 0; i < mm->entry_count; i++) {
        const struct limine_memmap_entry *e = mm->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;
        uint64_t start = (e->base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        uint64_t end = e->base + e->length;
        if (end > start && end - start >= bytes) return start;
    }
    return 0;
}

void pmm_init(struct limine_memmap_response *mm, uint64_t hhdm_offset) {
    hhdm = hhdm_offset;
    if (mm == NULL || hhdm == 0 || mm->entry_count == 0 || mm->entries == NULL) {
        kprintf("[B.1] FATAL: no memmap/HHDM response\n");
        hhdm = 0;
        return;
    }

    // 1) 只需要管理到「最大 usable 物理地址」；高端 MMIO 区间不必进 bitmap。
    for (uint64_t i = 0; i < mm->entry_count; i++) {
        const struct limine_memmap_entry *e = mm->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;
        uint64_t end = (e->base + e->length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        uint64_t pfn_end = end / PAGE_SIZE;
        if (pfn_end > max_pfn) max_pfn = pfn_end;
    }
    if (max_pfn == 0) {
        kprintf("[B.1] FATAL: no usable memory in memmap\n");
        hhdm = 0;
        return;
    }

    // 2) bitmap 大小按页对齐，方便整页保留自身。
    bitmap_bytes = ((max_pfn + 7) / 8 + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint64_t bitmap_phys = find_bitmap_region(mm, bitmap_bytes);
    if (bitmap_phys == 0) {
        kprintf("[B.1] FATAL: no %lu-byte region for bitmap\n", bitmap_bytes);
        hhdm = 0;
        return;
    }
    bitmap = (uint8_t *)(bitmap_phys + hhdm);

    // 3) 默认全部不可分配，再把 usable 区间逐页放开。
    kmemset(bitmap, 0xFF, (size_t)bitmap_bytes);
    for (uint64_t i = 0; i < mm->entry_count; i++) {
        const struct limine_memmap_entry *e = mm->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;
        uint64_t first = (e->base + PAGE_SIZE - 1) / PAGE_SIZE;
        uint64_t last = (e->base + e->length) / PAGE_SIZE;   // 开区间
        if (last > max_pfn) last = max_pfn;
        for (uint64_t pfn = first; pfn < last; pfn++) {
            if (!is_free(pfn)) {          // 仍是 1 => 尚未计数过
                set_free(pfn);
                total_pages++;
                free_pages++;
            }
        }
    }

    // 4) 保留第 0 页（避免物理地址 0 被当作分配成功/空指针）与 bitmap 自身。
    reserve_pfn(0);
    uint64_t b_first = bitmap_phys / PAGE_SIZE;
    uint64_t b_last = (bitmap_phys + bitmap_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t pfn = b_first; pfn < b_last; pfn++) reserve_pfn(pfn);
    hint_pfn = 1;

    kprintf("[B.1] hhdm=0x%016lx max_phys=0x%016lx bitmap@0x%016lx (%lu bytes)\n",
            hhdm, pmm_max_phys(), bitmap_phys, bitmap_bytes);
    kprintf("[B.1] usable=%lu pages (%lu MiB), free=%lu pages\n",
            total_pages, (total_pages * PAGE_SIZE) >> 20, free_pages);
}

uint64_t pmm_alloc_frame(void) {
    if (hhdm == 0) return 0;

    for (uint64_t pfn = hint_pfn; pfn < max_pfn; pfn++) {
        if (!is_free(pfn)) continue;
        set_used(pfn);
        free_pages--;
        hint_pfn = pfn + 1;
        return pfn * PAGE_SIZE;
    }
    // hint 之后没有空闲，再回头找前面被释放的页。
    for (uint64_t pfn = 1; pfn < hint_pfn && pfn < max_pfn; pfn++) {
        if (!is_free(pfn)) continue;
        set_used(pfn);
        free_pages--;
        hint_pfn = pfn + 1;
        return pfn * PAGE_SIZE;
    }
    return 0;
}

uint64_t pmm_alloc_frames(size_t count) {
    if (hhdm == 0 || count == 0 || count > (size_t)max_pfn) return 0;
    uint64_t run_start = 0, run_len = 0;
    for (uint64_t pfn = 1; pfn < max_pfn; pfn++) {
        if (is_free(pfn)) {
            if (run_len == 0) run_start = pfn;
            run_len++;
            if (run_len == (uint64_t)count) {
                for (uint64_t p = run_start; p <= pfn; p++) set_used(p);
                free_pages -= (uint64_t)count;
                return run_start * PAGE_SIZE;
            }
        } else {
            run_len = 0;
        }
    }
    return 0;
}

void pmm_free_frame(uint64_t phys) {
    if (hhdm == 0 || phys == 0 || (phys & (PAGE_SIZE - 1)) != 0) return;
    uint64_t pfn = phys / PAGE_SIZE;
    if (pfn >= max_pfn || is_free(pfn)) return;  // 越界或重复释放：忽略
    set_free(pfn);
    free_pages++;
    if (pfn < hint_pfn) hint_pfn = pfn;
}

void pmm_free_frames(uint64_t phys, size_t count) {
    for (size_t i = 0; i < count; i++) pmm_free_frame(phys + (uint64_t)i * PAGE_SIZE);
}

uint64_t pmm_total_pages(void)  { return total_pages; }
uint64_t pmm_free_pages(void)   { return free_pages; }
uint64_t pmm_hhdm_offset(void)  { return hhdm; }
uint64_t pmm_max_phys(void)     { return max_pfn * PAGE_SIZE; }
