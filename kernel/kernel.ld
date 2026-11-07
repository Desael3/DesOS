/* kernel.ld —— DesOS 内核链接脚本（对应 ProjectRoad A.4）
 *
 * 关键点（取自 Limine 原生协议）：
 *   - 内核被载入“更高半”，虚拟地址 >= 0xFFFFFFFF80000000（不可重定位内核
 *     必须自己声明更高半虚拟地址，bootloader 按此滑动加载）。
 *   - ENTRY(_start)：入口来自 kernel/asm/boot.asm。
 *   - .limine_reqs 段必须保留（KEEP），Limine 扫描它来发现请求并回填 response；
 *     它含已初始化的请求数据（magic），需随镜像加载，且运行时被 bootloader 写入，
 *     故放在可加载段（不要 NOLOAD）。
 *   - 各段按 4K 页对齐，方便后续自建页表。
 */

ENTRY(_start)

SECTIONS {
    /* 内核更高半基址（虚拟地址 VMA）。
     * 注意：物理载入地址 LMA 必须是一个真实可分配的低位物理地址，不能用更高半地址。
     * 否则 bootloader（Limine）会按 p_paddr(=0xFFFFFFFF80000000) 去分配/定位物理内存，
     * 导致内核镜像根本没被载入，跳转时取指 #PF → 三重错误。
     * 故用 AT(0x100000) 把整段镜像的 LMA 从较低物理地址排布，VMA 仍保持更高半。 */
    . = 0xFFFFFFFF80000000;

    kernel_start = .;

    /* 代码段（LMA 从物理 0x100000 起）*/
    .text : AT(0x100000) ALIGN(4K) {
        *(.text .text.*)
    }

    /* 只读数据（含字符串常量等）*/
    .rodata : ALIGN(4K) {
        *(.rodata .rodata.*)
    }

    /* Limine 请求段：必须保留，可被 bootloader 读取/回填。
     * 段名刻意不叫 ".limine_reqs" —— base revision 0 下 Limine 会把该名字的
     * 段当作「请求指针数组」（已废弃机制）解释，导致 panic：
     * "limine: .limine_reqs entry outside kernel image"，内核不会被引导。
     * 用普通名字，让 Limine 走标准的「扫描镜像 + 请求分隔符」路径。 */
    .limine_reqs_data : ALIGN(4K) {
        KEEP(*(.limine_reqs_data))
    }

    /* 已初始化数据 + 未初始化数据（BSS 并入同一 LOAD 段，保证被 bootloader 映射并清零）
     * 注意：不可对 .bss 用 (NOLOAD) —— Limine 等 bootloader 不会单独映射 NOLOAD 段，
     * 会导致 _start 里 call kernel_main 压栈到未映射的栈而触发 #PF → 三重错误。 */
    .data : ALIGN(4K) {
        *(.data .data.*)
    }

    .bss : ALIGN(4K) {
        bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        bss_end = .;
    }

    kernel_end = .;

    /* 丢弃对内核无用的段 */
    /DISCARD/ : {
        *(.note .note.*)
        *(.comment)
        *(.eh_frame .eh_frame.*)
    }
}
