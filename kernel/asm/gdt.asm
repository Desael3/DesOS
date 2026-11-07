; gdt.asm —— B.4 加载自有 GDT / TSS（x86_64 长模式）
;
; C 侧调用约定（System V AMD64）：
;   void gdt_flush(const void *gdtr, uint64_t code_sel, uint64_t data_sel);
;       rdi = gdtr 地址（16 位 limit + 64 位 base）
;       rsi = 内核代码选择子 0x08
;       rdx = 内核数据选择子 0x10
;   void tss_load(uint16_t tss_sel);
;       di  = TSS 选择子 0x28
;
; 关键点：lgdt 只换表，不刷新 CS 的隐藏描述符缓存；旧 CS=0x28 在新表里
; 会指向 TSS 描述符，因此必须用远返回（retf）显式重载 CS。

bits 64
section .text

global gdt_flush
gdt_flush:
    cli                         ; 换表/换 SS 期间不接受中断
    lgdt [rdi]
    ; NASM 会把 `mov ds, ax` 编码为 32 位形式 `mov ds, eax`，
    ; 只设置 ax 会让 eax 高 16 位残留垃圾 -> 无效选择子 -> #GP。
    ; 用 mov eax, edx 把整个 32 位清干净。
    mov eax, edx
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; 远返回重载 CS：栈上依次压入「新 CS」「目标 RIP」，retf 弹出两者
    push rsi
    lea rax, [rel .reload]
    push rax
    retfq                       ; 必须是 64 位远返回（48 CB），否则只弹 32 位 RIP/CS
.reload:
    ret

global tss_load
tss_load:
    and edi, 0xFFFF          ; 只保留 16 位选择子（NASM 的 ltr 只接受 16 位寄存器写法）
    ltr di
    ret
