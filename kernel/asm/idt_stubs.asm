; idt_stubs.asm —— B.5 中断/异常入口桩（对应 ProjectRoad B.5）
;
; 每个向量一个短桩，统一构造栈帧：
;   [rsp]      = vector
;   [rsp+8]    = error_code（CPU 未自动压入的，由桩补 0）
;   [rsp+16..] = CPU 压入的 rip/cs/rflags/rsp/ss
; 然后跳到 isr_common 保存通用寄存器并调用 C 处理函数 isr_common_handler。
;
; 带 CPU 错误码的异常：8, 10, 11, 12, 13, 14, 17, 21, 29, 30。
; 这些桩只压 vector，不补 error_code，保证所有向量栈布局一致。

bits 64

extern isr_common_handler
extern sched_switch_from_irq
global idt_flush
global isr_stub_table
global isr_stub_128

%macro STUB_NOERR 1
    push qword 0
    push qword %1
    jmp isr_common
%endmacro

%macro STUB_ERR 1
    push qword %1
    jmp isr_common
%endmacro

section .text

align 16
idt_flush:
    lidt [rdi]
    ret

isr_stub_0:
STUB_NOERR 0
isr_stub_1:
STUB_NOERR 1
isr_stub_2:
STUB_NOERR 2
isr_stub_3:
STUB_NOERR 3
isr_stub_4:
STUB_NOERR 4
isr_stub_5:
STUB_NOERR 5
isr_stub_6:
STUB_NOERR 6
isr_stub_7:
STUB_NOERR 7
isr_stub_8:
STUB_ERR 8
isr_stub_9:
STUB_NOERR 9
isr_stub_10:
STUB_ERR 10
isr_stub_11:
STUB_ERR 11
isr_stub_12:
STUB_ERR 12
isr_stub_13:
STUB_ERR 13
isr_stub_14:
STUB_ERR 14
isr_stub_15:
STUB_NOERR 15
isr_stub_16:
STUB_NOERR 16
isr_stub_17:
STUB_ERR 17
isr_stub_18:
STUB_NOERR 18
isr_stub_19:
STUB_NOERR 19
isr_stub_20:
STUB_NOERR 20
isr_stub_21:
STUB_ERR 21
isr_stub_22:
STUB_NOERR 22
isr_stub_23:
STUB_NOERR 23
isr_stub_24:
STUB_NOERR 24
isr_stub_25:
STUB_NOERR 25
isr_stub_26:
STUB_NOERR 26
isr_stub_27:
STUB_NOERR 27
isr_stub_28:
STUB_NOERR 28
isr_stub_29:
STUB_ERR 29
isr_stub_30:
STUB_ERR 30
isr_stub_31:
STUB_NOERR 31
isr_stub_32:
STUB_NOERR 32
isr_stub_33:
STUB_NOERR 33
isr_stub_34:
STUB_NOERR 34
isr_stub_35:
STUB_NOERR 35
isr_stub_36:
STUB_NOERR 36
isr_stub_37:
STUB_NOERR 37
isr_stub_38:
STUB_NOERR 38
isr_stub_39:
STUB_NOERR 39
isr_stub_40:
STUB_NOERR 40
isr_stub_41:
STUB_NOERR 41
isr_stub_42:
STUB_NOERR 42
isr_stub_43:
STUB_NOERR 43
isr_stub_44:
STUB_NOERR 44
isr_stub_45:
STUB_NOERR 45
isr_stub_46:
STUB_NOERR 46
isr_stub_47:
STUB_NOERR 47

; B.9 系统调用门（int 0x80），DPL=3 允许 ring3 调用
isr_stub_128:
STUB_NOERR 128

; 进入时：rsp 指向 vector。保存 15 个通用寄存器后，rdi = 栈帧地址。
; 栈对齐：CPU 中断帧 40 + err 8 + vector 8 = 56，再 push 15*8=120 字节，
; 合计 176 ≡ 0 (mod 16)，满足 SysV ABI 对 call 前 rsp 16 字节对齐的要求。
align 16
isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    cld
    call isr_common_handler

    ; B.7 抢占式调度切换点：handler 返回后、恢复寄存器之前。
    ; 传当前 rsp（指向本中断帧），返回「下一个线程」的 rsp；
    ; 两者栈布局完全相同，后续 pop...add rsp,16; iretq 可直接复用。
    mov rdi, rsp
    call sched_switch_from_irq
    mov rsp, rax

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16                 ; 丢弃 vector / error_code
    iretq

section .data
align 8
isr_stub_table:
dq isr_stub_0
dq isr_stub_1
dq isr_stub_2
dq isr_stub_3
dq isr_stub_4
dq isr_stub_5
dq isr_stub_6
dq isr_stub_7
dq isr_stub_8
dq isr_stub_9
dq isr_stub_10
dq isr_stub_11
dq isr_stub_12
dq isr_stub_13
dq isr_stub_14
dq isr_stub_15
dq isr_stub_16
dq isr_stub_17
dq isr_stub_18
dq isr_stub_19
dq isr_stub_20
dq isr_stub_21
dq isr_stub_22
dq isr_stub_23
dq isr_stub_24
dq isr_stub_25
dq isr_stub_26
dq isr_stub_27
dq isr_stub_28
dq isr_stub_29
dq isr_stub_30
dq isr_stub_31
dq isr_stub_32
dq isr_stub_33
dq isr_stub_34
dq isr_stub_35
dq isr_stub_36
dq isr_stub_37
dq isr_stub_38
dq isr_stub_39
dq isr_stub_40
dq isr_stub_41
dq isr_stub_42
dq isr_stub_43
dq isr_stub_44
dq isr_stub_45
dq isr_stub_46
dq isr_stub_47
