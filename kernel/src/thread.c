// thread.c —— B.8 进程/线程模型（内核线程，对应 ProjectRoad B.8）

#include "thread.h"
#include "sched.h"
#include "syscall.h"
#include "idt.h"
#include "gdt.h"
#include "vmm.h"
#include "elf.h"
#include "heap.h"
#include "kstring.h"
#include <stdint.h>

#define THREAD_STACK_SIZE (16 * 1024)

static uint64_t created_count;

// 首次启动入口：不是被 call 进来的，而是由 iretq 跳进来。
// 所以栈上构造的 rsp 特意留成「刚 call 过」的样子（rsp % 16 == 8）。
void thread_bootstrap(void) {
    struct thread *t = sched_current();
    if (t != NULL && t->entry != NULL) t->entry(t->arg);
    thread_exit();               // 正常不返回：SYS_EXIT 会切走
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

struct thread *thread_create(const char *name, void (*entry)(void *), void *arg) {
    struct thread *t = (struct thread *)kmalloc(sizeof(struct thread));
    if (t == NULL) return NULL;
    uint8_t *stack = (uint8_t *)kmalloc(THREAD_STACK_SIZE);
    if (stack == NULL) {
        kfree(t);
        return NULL;
    }
    kmemset(t, 0, sizeof(*t));
    t->id = (int)(++created_count);
    t->state = THREAD_READY;
    t->cr3 = vmm_current_pml4_phys();
    t->entry = entry;
    t->arg = arg;
    t->stack_base = (uint64_t)stack;
    for (int i = 0; name != NULL && i < 15 && name[i] != '\0'; i++) t->name[i] = name[i];

    // 在栈顶构造一份完整「中断帧」，与 isr_common 保存/恢复的布局一致。
    // 这样第一次被调度时，统一的 pop...add rsp,16; iretq 就能启动该线程。
    uint64_t top = ((uint64_t)stack + THREAD_STACK_SIZE) & ~15ULL;
    t->kernel_stack_top = top;
    struct interrupt_frame *f =
        (struct interrupt_frame *)(top - sizeof(struct interrupt_frame));
    kmemset(f, 0, sizeof(*f));
    f->rip    = (uint64_t)&thread_bootstrap;
    f->cs     = KERNEL_CODE_SEL;
    f->rflags = 0x202;                      // IF=1：运行期间可被 PIT 抢占
    f->rsp    = (uint64_t)(f + 1) - 8;      // 模拟 call 压入的返回地址
    f->ss     = KERNEL_DATA_SEL;
    t->rsp = (uint64_t)f;

    sched_enqueue(t);
    return t;
}

// B.10：用户态线程。初始中断帧直接指定 ring3 的 CS/SS/RSP/RIP，
// 调度器切到它时，统一的 iretq 会把 CPU 从 ring0 降到 ring3。
struct thread *thread_create_user(const char *name, const struct user_image *img) {
    if (img == NULL || img->pml4_phys == 0) return NULL;
    struct thread *t = (struct thread *)kmalloc(sizeof(struct thread));
    if (t == NULL) return NULL;
    uint8_t *stack = (uint8_t *)kmalloc(THREAD_STACK_SIZE);
    if (stack == NULL) {
        kfree(t);
        return NULL;
    }
    kmemset(t, 0, sizeof(*t));
    t->id = (int)(++created_count);
    t->state = THREAD_READY;
    t->cr3 = img->pml4_phys;
    t->stack_base = (uint64_t)stack;
    for (int i = 0; name != NULL && i < 15 && name[i] != '\0'; i++) t->name[i] = name[i];

    uint64_t top = ((uint64_t)stack + THREAD_STACK_SIZE) & ~15ULL;
    t->kernel_stack_top = top;                 // 写进 TSS.rsp0
    struct interrupt_frame *f =
        (struct interrupt_frame *)(top - sizeof(struct interrupt_frame));
    kmemset(f, 0, sizeof(*f));
    f->rip    = img->entry;
    f->cs     = USER_CODE_SEL | 3;             // 0x23，RPL=3
    f->rflags = 0x202;                         // IF=1
    f->rsp    = img->stack_top;
    f->ss     = USER_DATA_SEL | 3;              // 0x1B，RPL=3
    t->rsp = (uint64_t)f;

    sched_enqueue(t);
    return t;
}

struct thread *thread_current(void) { return sched_current(); }

void thread_exit(void) {
    do_syscall(SYS_EXIT, 0, 0, 0);
}

uint64_t thread_count(void) { return created_count; }
