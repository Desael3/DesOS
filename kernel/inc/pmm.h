#ifndef DESOS_PMM_H
#define DESOS_PMM_H

#include <stdint.h>
#include <stddef.h>
#include <limine.h>

// B.1 物理内存管理：页帧分配器（bitmap）
//
// 设计要点：
//   - 数据源是 Limine 的 memmap 请求；只有 LIMINE_MEMMAP_USABLE 的页可被分配。
//     内核镜像、帧缓冲、ACPI、MMIO 等在 memmap 里是其它类型，天然不会分出去。
//   - bitmap 覆盖 [0, 最大 usable 物理地址)，每页 1 bit，1=已用/不可分配，0=空闲。
//     bitmap 自身放在一块 usable 内存里，并把自身占用的页标为已用，避免自覆盖。
//   - 所有物理地址都通过 HHDM 偏移（phys + hhdm_offset）访问；base revision 1
//     下 Limine 不再保证恒等映射。
//   - 只分配页对齐的 4 KiB 页帧；返回的是「物理地址」，0 表示失败。

#define PAGE_SIZE 4096ULL

// 用 Limine 的 memmap / HHDM 响应初始化。失败时 pmm_hhdm_offset() 返回 0。
void pmm_init(struct limine_memmap_response *mm, uint64_t hhdm_offset);

uint64_t pmm_alloc_frame(void);                 // 单页；失败返回 0
uint64_t pmm_alloc_frames(size_t count);        // 连续 count 页；失败返回 0
void pmm_free_frame(uint64_t phys);
void pmm_free_frames(uint64_t phys, size_t count);

uint64_t pmm_total_pages(void);                 // memmap 中的 usable 总页数
uint64_t pmm_free_pages(void);                  // 当前可分配页数
uint64_t pmm_hhdm_offset(void);                 // 0 表示未初始化
uint64_t pmm_max_phys(void);                    // bitmap 覆盖的物理地址上界

#endif // DESOS_PMM_H
