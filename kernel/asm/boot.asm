; boot.asm —— DesOS 启动桩（Limine 原生协议，x86_64）
; 对应 ProjectRoad A.2：建立栈、开启/利用 HHDM、把控制权交给 kernel_main。
;
; 约定（取自 tools/limine/limine-12.9.6/limine-protocol/PROTOCOL.md）：
;   - 内核被 Limine 载入更高半（虚拟地址 >= 0xFFFFFFFF80000000）。
;   - 入口 _start 时已是 64 位长模式：CS=0x28，DS/ES/SS/FS/GS=0x30，GDT 已由 Limine 设好。
;     （B.4 会在 kernel_main 里换上 DesOS 自有 GDT/TSS，届时 CS=0x08、TR=0x28。）
;   - 所有通用寄存器（rax-r15）为 0；栈由 Limine 提供（>=64KiB，返回地址为 0）。
;   - HHDM（Higher Half Direct Map）已由 Limine 在进内核前开启，内核后续
;     通过 LIMINE_HHDM_REQUEST 取得 hhdm_offset 来访问物理内存，本桩无需额外操作。
;   - boot info 不用 rdi 传递，统一走「请求/响应」模型（见 A.3 的 C 代码）。

bits 64

global _start
; kernel_main 由 C 实现（见 A.3），在此声明为外部符号，链接时解析。
extern kernel_main

; 内核栈放在 BSS 段；BSS 地址属于更高半（链接脚本 A.4 把内核载入 0xFFFFFFFF80000000+），
; 因此该栈地址已被 Limine 直接映射，开箱即用。
section .bss
align 16
stack_bottom:
    resb 65536              ; 64 KiB 内核栈
stack_top:

section .text
_start:
    ; 1) 建立自有、已知位置的更高半内核栈（替换 Limine 提供的临时栈）。
    mov rsp, stack_top
    mov rbp, rsp

    ; 按 System V AMD64 ABI 清除方向标志，保证后续 REP 类指令行为正确。
    cld

    ; 2) HHDM 已由 Limine 开启（见顶部注释），此处只需确保 64 位环境就绪即可，
    ;    实际物理内存访问交给定 kernel_main 通过请求拿到的 hhdm_offset。

    ; 3) 把控制权交给内核入口（C 实现，见 A.3）。kernel_main 不应返回。
    call kernel_main

    ; 若 kernel_main 意外返回，则关中断并停机。
.hang:
    cli
    hlt
    jmp .hang
