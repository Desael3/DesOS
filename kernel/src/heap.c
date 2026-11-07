// heap.c —— B.3 内核堆：kmalloc / kfree（对应 ProjectRoad B.3）

#include "heap.h"
#include "vmm.h"
#include "pmm.h"
#include "kstring.h"
#include "kprintf.h"

#define BLK_MAGIC 0xDE50B10CU

struct block {
    uint64_t size;              // 有效载荷字节数
    struct block *next;         // 按虚拟地址顺序
    struct block *prev;
    uint32_t free;
    uint32_t magic;
};

#define HDR_SIZE ((sizeof(struct block) + 15ULL) & ~15ULL)

static struct block *head;
static struct block *tail;
static uint64_t mapped_bytes;
static uint64_t alloc_count;

static void *payload(struct block *b) {
    return (void *)((uint64_t)b + HDR_SIZE);
}

static int adjacent(struct block *a, struct block *b) {
    return (uint64_t)a + HDR_SIZE + a->size == (uint64_t)b;
}

// 把 b 合并进它后面的空闲块（要求虚拟相邻）。返回合并后的块。
static struct block *merge_with_next(struct block *b) {
    struct block *n = b->next;
    if (n == NULL || !n->free || !adjacent(b, n)) return b;
    b->size += HDR_SIZE + n->size;
    b->next = n->next;
    if (n->next) n->next->prev = b;
    else tail = b;
    n->magic = 0;
    return b;
}

// 扩展堆：映射 pages 个新页，并在新区域上建一个空闲块（可与前块合并）。
static int heap_extend(uint64_t need_bytes) {
    uint64_t pages = (need_bytes + HDR_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
    if (mapped_bytes + pages * PAGE_SIZE > KHEAP_MAX_SIZE) return -1;

    uint64_t start = mapped_bytes;
    for (uint64_t i = 0; i < pages; i++) {
        uint64_t phys = pmm_alloc_frame();
        if (phys == 0) goto rollback;
        int rc = vmm_map(KHEAP_BASE + mapped_bytes, phys, VMM_PRESENT | VMM_WRITE);
        if (rc != 0) {
            pmm_free_frame(phys);
            goto rollback;
        }
        mapped_bytes += PAGE_SIZE;
    }

    struct block *b = (struct block *)(KHEAP_BASE + start);
    b->size = pages * PAGE_SIZE - HDR_SIZE;
    b->next = NULL;
    b->prev = tail;
    b->free = 1;
    b->magic = BLK_MAGIC;
    if (tail) tail->next = b;
    else head = b;
    tail = b;
    if (b->prev && b->prev->free) merge_with_next(b->prev);
    return 0;

rollback:
    while (mapped_bytes > start) {
        mapped_bytes -= PAGE_SIZE;
        uint64_t phys = vmm_get_phys(KHEAP_BASE + mapped_bytes);
        vmm_unmap(KHEAP_BASE + mapped_bytes);
        if (phys) pmm_free_frame(phys);
    }
    return -1;
}

void heap_init(void) {
    head = NULL;
    tail = NULL;
    mapped_bytes = 0;
    alloc_count = 0;
    kprintf("[B.3] heap base=0x%016lx max=%lu MiB (on-demand paging)\n",
            (uint64_t)KHEAP_BASE, (uint64_t)(KHEAP_MAX_SIZE >> 20));
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    size = (size + 15) & ~(size_t)15;

    for (;;) {
        struct block *b = head;
        while (b != NULL && !(b->free && b->size >= size)) b = b->next;
        if (b != NULL) {
            // 剩余空间足以再放一个块头 + 最小载荷时才拆分
            if (b->size >= size + HDR_SIZE + 16) {
                struct block *n = (struct block *)((uint64_t)b + HDR_SIZE + size);
                n->size = b->size - size - HDR_SIZE;
                n->prev = b;
                n->next = b->next;
                n->free = 1;
                n->magic = BLK_MAGIC;
                if (b->next) b->next->prev = n;
                else tail = n;
                b->next = n;
                b->size = size;
            }
            b->free = 0;
            alloc_count++;
            return payload(b);
        }
        if (heap_extend(size) != 0) return NULL;
    }
}

void kfree(void *ptr) {
    if (ptr == NULL) return;
    struct block *b = (struct block *)((uint64_t)ptr - HDR_SIZE);
    if (b->magic != BLK_MAGIC || b->free) {
        kprintf("[B.3] WARN: invalid kfree(0x%016lx)\n", (uint64_t)(uintptr_t)ptr);
        return;
    }
    b->free = 1;
    struct block *merged = merge_with_next(b);   // 与后继空闲块合并
    if (merged->prev && merged->prev->free) merge_with_next(merged->prev);
}

void *kcalloc(size_t count, size_t size) {
    if (count != 0 && size > (size_t)0xFFFFFFFFFFFFFFFFULL / count) return NULL;
    size_t total = count * size;
    void *p = kmalloc(total);
    if (p) kmemset(p, 0, total);
    return p;
}

uint64_t heap_mapped_bytes(void) { return mapped_bytes; }
uint64_t heap_alloc_count(void)  { return alloc_count; }
