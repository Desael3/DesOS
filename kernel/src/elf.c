// elf.c —— B.10 ELF64 用户程序加载器（对应 ProjectRoad B.10）

#include "elf.h"
#include "pmm.h"
#include "vmm.h"
#include "kstring.h"
#include "kprintf.h"
#include <stdint.h>

#define PT_LOAD           1
#define ET_EXEC           2
#define EM_X86_64         62
#define USER_STACK_PAGES  4
// 低半 canonical 上界是 0x00007FFFFFFFFFFF；4 页栈必须整体落在它之内。
// 0x7FFFFFFFC000 + 4*4K = 0x800000000000 正好是「栈顶端（不含）」，
// 最后一页范围 [0x7FFFFFFFF000, 0x800000000000)，仍是合法低半地址。
#define USER_STACK_BOTTOM 0x7FFFFFFFC000ULL
#define USER_STACK_TOP    (USER_STACK_BOTTOM + USER_STACK_PAGES * PAGE_SIZE)

struct elf64_ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed));

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed));

static uint64_t loaded_segments;

int elf_load(const uint8_t *image, uint64_t size, struct user_image *out) {
    loaded_segments = 0;
    if (image == NULL || out == NULL || size < sizeof(struct elf64_ehdr)) return -1;

    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)image;
    if (kmemcmp(eh->e_ident, "\x7f" "ELF", 4) != 0) return -2;
    if (eh->e_ident[4] != 2 || eh->e_ident[5] != 1) return -3;   // ELFCLASS64 / LE
    if (eh->e_type != ET_EXEC || eh->e_machine != EM_X86_64) return -4;
    if (eh->e_phnum == 0 || eh->e_phentsize < sizeof(struct elf64_phdr)) return -5;

    uint64_t pml4 = vmm_create_user_pml4();
    if (pml4 == 0) return -6;
    uint64_t hhdm = pmm_hhdm_offset();

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        uint64_t off = eh->e_phoff + (uint64_t)i * eh->e_phentsize;
        if (off + sizeof(struct elf64_phdr) > size) return -7;
        struct elf64_phdr ph;
        kmemcpy(&ph, image + off, sizeof(ph));
        if (ph.p_type != PT_LOAD || ph.p_memsz == 0) continue;
        if (ph.p_offset + ph.p_filesz > size) return -8;

        uint64_t first = ph.p_vaddr & ~(PAGE_SIZE - 1);
        uint64_t last  = (ph.p_vaddr + ph.p_memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        uint64_t seg_start = ph.p_vaddr;
        uint64_t seg_end   = ph.p_vaddr + ph.p_filesz;

        for (uint64_t va = first; va < last; va += PAGE_SIZE) {
            uint64_t phys = pmm_alloc_frame();
            if (phys == 0) return -9;
            uint8_t *dst = (uint8_t *)(phys + hhdm);
            kmemset(dst, 0, (size_t)PAGE_SIZE);

            uint64_t cs = (va > seg_start) ? va : seg_start;
            uint64_t ce = (va + PAGE_SIZE < seg_end) ? (va + PAGE_SIZE) : seg_end;
            if (cs < ce) {
                kmemcpy(dst + (cs - va),
                        image + ph.p_offset + (cs - seg_start),
                        (size_t)(ce - cs));
            }
            if (vmm_map_user(pml4, va, phys,
                             VMM_PRESENT | VMM_WRITE | VMM_USER) != 0) return -10;
        }
        loaded_segments++;
        kprintf("[B.10] PT_LOAD off=0x%lx vaddr=0x%016lx filesz=%lu memsz=%lu flags=0x%x\n",
                ph.p_offset, ph.p_vaddr, ph.p_filesz, ph.p_memsz, (unsigned)ph.p_flags);
    }
    if (loaded_segments == 0) return -11;

    // 用户栈：4 页，映射到固定高地址；栈顶放一个假的返回地址，
    // 使 _start 作为普通 C 函数进入时 rsp % 16 == 8（符合 SysV ABI）。
    uint64_t top_phys = 0;
    for (uint64_t i = 0; i < USER_STACK_PAGES; i++) {
        uint64_t phys = pmm_alloc_frame();
        if (phys == 0) return -12;
        kmemset((void *)(phys + hhdm), 0, (size_t)PAGE_SIZE);
        int mrc = vmm_map_user(pml4, USER_STACK_BOTTOM + i * PAGE_SIZE, phys,
                               VMM_PRESENT | VMM_WRITE | VMM_USER);
        if (mrc != 0) {
            kprintf("[B.10] stack map failed i=%lu rc=%d va=0x%016lx\n",
                    i, mrc, USER_STACK_BOTTOM + i * PAGE_SIZE);
            return -13;
        }
        top_phys = phys;
    }
    uint64_t *sp = (uint64_t *)(top_phys + hhdm + PAGE_SIZE - 8);
    sp[0] = 0;

    out->entry = eh->e_entry;
    out->stack_top = USER_STACK_TOP - 8;
    out->pml4_phys = pml4;
    kprintf("[B.10] ELF loaded: entry=0x%016lx rsp=0x%016lx pml4=0x%016lx segments=%lu\n",
            out->entry, out->stack_top, out->pml4_phys, loaded_segments);
    return 0;
}

uint64_t elf_loaded_segments(void) { return loaded_segments; }
