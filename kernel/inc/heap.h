#ifndef DESOS_HEAP_H
#define DESOS_HEAP_H

#include <stdint.h>
#include <stddef.h>

// B.3 内核堆：kmalloc / kfree
//   - 虚拟区间从 KHEAP_BASE 起（独立 PML4 项，见 vmm.h），按需增长；
//   - 每次增长向 B.1 申请物理页，再由 B.2 的 vmm_map 接上虚拟地址；
//   - 块管理用「按虚拟地址有序的双向空闲链表 + 首次适配 + 相邻合并」。
//   - 返回地址 16 字节对齐；kfree(NULL) 是安全的空操作。

void  heap_init(void);
void *kmalloc(size_t size);
void  kfree(void *ptr);
void *kcalloc(size_t count, size_t size);
uint64_t heap_mapped_bytes(void);   // 已映射的堆虚拟空间大小
uint64_t heap_alloc_count(void);    // 累计成功分配次数

#endif // DESOS_HEAP_H
