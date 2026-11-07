// sched.c —— B.7 调度器（round-robin，对应 ProjectRoad B.7）
//
// 切换点唯一：isr_common 里 handler 返回后、恢复寄存器之前。
// 因此「被挂起的 rsp」永远指向一份中断帧（15 regs + vec/err + iret frame），
// 新线程恢复时走同一条 pop/iretq 路径即可。

#include "sched.h"
#include "vmm.h"
#include "gdt.h"
#include "kstring.h"
#include <stdint.h>

static inline void write_cr3(uint64_t phys) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(phys) : "memory");
}

static struct thread main_thread;
static struct thread *current;
static struct thread *ready_head;
static struct thread *ready_tail;
static uint64_t switch_count;
static volatile int need_resched;
static int enabled;

void sched_init(void) {
    ready_head = NULL;
    ready_tail = NULL;
    current = NULL;
    switch_count = 0;
    need_resched = 0;
    enabled = 0;

    kmemset(&main_thread, 0, sizeof(main_thread));
    main_thread.id = 0;
    main_thread.state = THREAD_RUNNING;
    main_thread.cr3 = vmm_current_pml4_phys();
    main_thread.name[0] = 'm';
    main_thread.name[1] = 'a';
    main_thread.name[2] = 'i';
    main_thread.name[3] = 'n';
    // rsp 置 0：第一次被抢占时由 sched_switch_from_irq 写入真实栈指针
    current = &main_thread;
}

void sched_enable(void) { enabled = 1; }

void sched_request(void) {
    if (enabled) need_resched = 1;
}

void sched_enqueue(struct thread *t) {
    if (t == NULL) return;
    t->next = NULL;
    if (ready_head == NULL) {
        ready_head = ready_tail = t;
    } else {
        ready_tail->next = t;
        ready_tail = t;
    }
}

static struct thread *sched_dequeue(void) {
    struct thread *t = ready_head;
    if (t == NULL) return NULL;
    ready_head = t->next;
    if (ready_head == NULL) ready_tail = NULL;
    t->next = NULL;
    return t;
}

struct thread *sched_current(void) { return current; }
uint64_t sched_switch_count(void) { return switch_count; }

uint64_t sched_switch_from_irq(uint64_t current_rsp) {
    if (current == NULL) return current_rsp;
    if (!need_resched && current->state == THREAD_RUNNING) return current_rsp;

    need_resched = 0;
    struct thread *prev = current;
    uint64_t prev_cr3 = prev->cr3;
    current->rsp = current_rsp;

    if (current->state == THREAD_RUNNING) {
        current->state = THREAD_READY;
        sched_enqueue(current);
    }
    // DEAD 线程不再入队

    struct thread *next = sched_dequeue();
    if (next == NULL) {
        // 没有别的就绪线程：继续当前线程（main 永远在，正常不会走到这里）
        current->state = THREAD_RUNNING;
        return current_rsp;
    }

    next->state = THREAD_RUNNING;
    // B.10：用户线程需要自己的内核栈（ring3->ring0 时 CPU 取 TSS.rsp0）
    // 与独立地址空间（切 CR3）。内核更高半映射在两套 PML4 里都存在，
    // 所以本函数切完 CR3 后仍能继续执行。
    if (next->kernel_stack_top != 0) gdt_set_kernel_stack(next->kernel_stack_top);
    if (next->cr3 != 0 && next->cr3 != prev_cr3) write_cr3(next->cr3);
    current = next;
    switch_count++;
    return next->rsp;
}
