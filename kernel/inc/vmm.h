#ifndef DESOS_VMM_H
#define DESOS_VMM_H

#include <stdint.h>
#include "pmm.h"

// B.2 虚拟内存：4 级页表管理
//
// Limine 进内核时已经建好并启用分页（内核更高半镜像 + 覆盖全部物理内存的 HHDM，
// 可能用 2MiB/1GiB 大页）。B.2 不推倒重建，而是「接管」当前 PML4：
//   - 遍历 4 级页表查询虚拟地址 -> 物理地址（支持大页）；
//   - 按需建立 PDPT/PD/PT 并映射 4 KiB 页（页表页由 B.1 的 PMM 提供）；
//   - 在独立的 PML4 项里划出内核堆与测试用虚拟地址空间，不与已有映射冲突。
// 用户态地址空间/换页等留到 B.10 用户态 ELF 加载时再扩展。

#define VMM_PRESENT (1ULL << 0)
#define VMM_WRITE   (1ULL << 1)            // 可写
#define VMM_USER    (1ULL << 2)
#define VMM_NX      (1ULL << 63)
#define VMM_PS      (1ULL << 7)            // PDPTE/PDE 的大页位
#define VMM_PCD     (1ULL << 4)            // 页级 cache-disable（B.6b 的 APIC MMIO）

// 内核虚拟地址空间规划（均为 canonical 更高半地址）
//   PML4 index 400：内核堆（B.3）
//   PML4 index 402：B.2 映射自检用
#define KHEAP_BASE  0xFFFFC80000000000ULL
#define KHEAP_MAX_SIZE (64ULL * 1024 * 1024)
#define VMM_TEST_VA 0xFFFFC90000000000ULL

void     vmm_init(void);
int      vmm_map(uint64_t va, uint64_t phys, uint64_t flags);
int      vmm_unmap(uint64_t va);
uint64_t vmm_get_phys(uint64_t va);          // 0 表示未映射
uint64_t vmm_current_pml4_phys(void);

// B.10：创建独立用户地址空间。新 PML4 只复制「更高半」映射（内核镜像 +
// HHDM），低半留空给用户程序；用户页映射需带 VMM_USER 位才能被 ring3 访问。
uint64_t vmm_create_user_pml4(void);
int      vmm_map_user(uint64_t pml4, uint64_t va, uint64_t phys, uint64_t flags);
// D.2c：只读查询用户地址空间某 4K 对齐 VA 的 PTE 原始值（0=未映射）
uint64_t vmm_get_pte_user(uint64_t pml4, uint64_t va);

#endif // DESOS_VMM_H
