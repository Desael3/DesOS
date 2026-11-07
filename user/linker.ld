/* user/linker.ld —— 用户态程序链接脚本（ELF64，固定加载地址 0x400000） */
ENTRY(_start)

SECTIONS {
    . = 0x400000;

    .text : ALIGN(4K)   { *(.text .text.*) }
    .rodata : ALIGN(4K) { *(.rodata .rodata.*) }
    .data : ALIGN(4K)   { *(.data .data.*) }
    .bss : ALIGN(4K)    { *(.bss .bss.*) *(COMMON) }

    /DISCARD/ : {
        *(.note .note.*)
        *(.comment)
        *(.eh_frame .eh_frame.*)
    }
}
