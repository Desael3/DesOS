#ifndef DESOS_ELF_H
#define DESOS_ELF_H

#include <stdint.h>

// B.10 ELF64 用户程序加载器
//   - 镜像是编译进内核 .rodata 的静态 ELF64（user/hello.c）；
//   - 为每个程序建独立 PML4，把 PT_LOAD 段映射到用户虚拟地址；
//   - 另映射 4 页用户栈，返回 entry / rsp / cr3，交给线程调度器 iretq 进 ring3。
struct user_image {
    uint64_t entry;         // ELF e_entry
    uint64_t stack_top;     // 用户态初始 rsp
    uint64_t pml4_phys;     // 用户地址空间
};

int      elf_load(const uint8_t *image, uint64_t size, struct user_image *out);
uint64_t elf_loaded_segments(void);   // 最近一次加载的 PT_LOAD 段数（自检用）

// 内嵌的用户态 ELF 镜像（由 NASM incbin 放进 .rodata，见 tools/build_user.py）
extern const uint8_t user_elf_start[];
extern const uint8_t user_elf_end[];

#endif // DESOS_ELF_H
