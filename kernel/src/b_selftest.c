// b_selftest.c —— 阶段 B 自检（B.1–B.11，对应 ProjectRoad 阶段 B）
//
// 原则：每一步都「真分配、真写入、真读回、真释放」，不靠静态推断；
// 中断/异常部分真触发 #BP、#PF、硬件 IRQ0、抢占式线程切换、int 0x80，
// 用户态部分真加载 ELF、真 iretq 进 ring3，Shell 部分真执行命令并拉起用户程序。

#include "b_selftest.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "kstring.h"
#include "kprintf.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "apic.h"
#include "sched.h"
#include "timer.h"
#include "syscall.h"
#include "thread.h"
#include "elf.h"
#include "shell.h"
#include "kbd.h"
#include <stdint.h>

static int test_pmm(void) {
    uint64_t before = pmm_free_pages();
    int ok = 1;

    uint64_t a = pmm_alloc_frame();
    uint64_t b = pmm_alloc_frame();
    uint64_t c = pmm_alloc_frame();
    ok &= (a != 0 && b != 0 && c != 0);
    ok &= (a != b && b != c && a != c);
    ok &= ((a | b | c) & (PAGE_SIZE - 1)) == 0;

    uint64_t h = pmm_hhdm_offset();
    if (a) {
        volatile uint64_t *pa = (volatile uint64_t *)(a + h);
        *pa = 0xDE50B1AA00000001ULL;
        ok &= (*pa == 0xDE50B1AA00000001ULL);
    }

    uint64_t run = pmm_alloc_frames(4);
    ok &= (run != 0 && (run & (PAGE_SIZE - 1)) == 0);
    if (run) {
        for (uint64_t i = 0; i < 4; i++) {
            volatile uint32_t *q = (volatile uint32_t *)(run + i * PAGE_SIZE + h);
            *q = 0xB1000000U + (uint32_t)i;
        }
        for (uint64_t i = 0; i < 4; i++) {
            volatile uint32_t *q = (volatile uint32_t *)(run + i * PAGE_SIZE + h);
            ok &= (*q == 0xB1000000U + (uint32_t)i);
        }
    }

    pmm_free_frame(a);
    pmm_free_frame(b);
    pmm_free_frame(c);
    pmm_free_frames(run, 4);
    ok &= (pmm_free_pages() == before);

    kprintf("[B.1] selftest: %s (total=%lu free=%lu pages)\n",
            ok ? "PASS" : "FAIL", pmm_total_pages(), pmm_free_pages());
    return ok ? 0 : 1;
}

static int test_vmm(void) {
    int ok = 1;

    uint64_t kphys = vmm_get_phys((uint64_t)(uintptr_t)&b_selftest_run);
    ok &= (kphys != 0);
    kprintf("[B.2] kernel text phys=0x%016lx\n", kphys);

    uint64_t h = pmm_hhdm_offset();
    uint64_t p = pmm_alloc_frame();
    ok &= (p != 0);
    if (p == 0) {
        kprintf("[B.2] selftest: FAIL (no free frame)\n");
        return 1;
    }
    ok &= (vmm_get_phys(p + h) == p);

    int rc = vmm_map(VMM_TEST_VA, p, VMM_PRESENT | VMM_WRITE);
    ok &= (rc == 0);
    if (rc == 0) {
        volatile uint64_t *v = (volatile uint64_t *)VMM_TEST_VA;
        *v = 0x00C0FFEE12345678ULL;
        ok &= (*v == 0x00C0FFEE12345678ULL);
        ok &= (vmm_get_phys(VMM_TEST_VA) == p);
        kprintf("[B.2] map 0x%016lx -> phys 0x%016lx, readback=0x%016lx\n",
                (uint64_t)VMM_TEST_VA, vmm_get_phys(VMM_TEST_VA), *v);
        ok &= (vmm_unmap(VMM_TEST_VA) == 0);
        ok &= (vmm_get_phys(VMM_TEST_VA) == 0);
    }
    pmm_free_frame(p);

    kprintf("[B.2] selftest: %s (pml4=0x%016lx)\n",
            ok ? "PASS" : "FAIL", vmm_current_pml4_phys());
    return ok ? 0 : 1;
}

static int test_heap(void) {
    int ok = 1;

    uint8_t *a = (uint8_t *)kmalloc(32);
    uint8_t *b = (uint8_t *)kmalloc(128);
    uint8_t *c = (uint8_t *)kmalloc(4096);
    ok &= (a != NULL && b != NULL && c != NULL);
    ok &= (a != b && b != c && a != c);
    ok &= (((uint64_t)a | (uint64_t)b | (uint64_t)c) & 15ULL) == 0;

    if (a && b && c) {
        kmemset(a, 0xA5, 32);
        kmemset(b, 0x5A, 128);
        kmemset(c, 0x3C, 4096);
        for (int i = 0; i < 32; i++)   ok &= (a[i] == 0xA5);
        for (int i = 0; i < 128; i++)  ok &= (b[i] == 0x5A);
        for (int i = 0; i < 4096; i++) ok &= (c[i] == 0x3C);
        ok &= (kmemcmp(a, b, 32) != 0);   // 两块内容互不干扰
    }

    uint8_t *b_addr = b;
    kfree(b);
    uint8_t *d = (uint8_t *)kmalloc(64);
    ok &= (d == b_addr);                   // 释放后的块被复用

    // 大分配：必然触发多次按需分页建表，验证堆增长链路。
    uint8_t *big = (uint8_t *)kmalloc(1024 * 1024);
    ok &= (big != NULL);
    if (big) {
        for (uint32_t i = 0; i < 1024 * 1024; i++) big[i] = (uint8_t)(i * 7 + 3);
        for (uint32_t i = 0; i < 1024 * 1024; i++)
            ok &= (big[i] == (uint8_t)(i * 7 + 3));
        kfree(big);
    }

    kfree(a);
    kfree(c);
    kfree(d);

    kprintf("[B.3] selftest: %s (mapped=%lu KiB allocs=%lu)\n",
            ok ? "PASS" : "FAIL", heap_mapped_bytes() >> 10, heap_alloc_count());
    return ok ? 0 : 1;
}

static int test_gdt(void) {
    int ok = 1;
    ok &= (gdt_kernel_cs() == KERNEL_CODE_SEL);
    ok &= (gdt_task_register() == TSS_SEL);
    ok &= (gdt_base() != 0 && gdt_tss_base() != 0);
    ok &= (gdt_limit() == 7 * 8 - 1);
    ok &= (gdt_tss_rsp0() != 0 && (gdt_tss_rsp0() & 15ULL) == 0);

    kprintf("[B.4] selftest: %s (gdtr=0x%016lx/%u cs=0x%x tr=0x%x rsp0=0x%016lx)\n",
            ok ? "PASS" : "FAIL", gdt_base(), (unsigned)gdt_limit(),
            (unsigned)gdt_kernel_cs(), (unsigned)gdt_task_register(), gdt_tss_rsp0());
    return ok ? 0 : 1;
}

static int test_idt(void) {
    int ok = 1;
    ok &= (idt_base() != 0 && idt_limit() == 256 * 16 - 1);

    // 1) 软件断点异常：handler 打印后 iretq 返回，后续指令继续执行。
    uint64_t bp0 = idt_breakpoint_count();
    __asm__ volatile ("int3");
    ok &= (idt_breakpoint_count() == bp0 + 1);

    // 2) 缺页处理：故意访问未映射地址，#PF 里按需分配并映射，重试指令应成功。
    uint64_t pf0 = idt_pagefault_count();
    uint64_t va = 0xFFFFC90000002000ULL;      // 与 B.2 测试同 PML4 项，PTE 尚未映射
    idt_arm_demand_fault(va);
    volatile uint64_t *p = (volatile uint64_t *)va;
    *p = 0xDE50DEADBEEF0001ULL;
    ok &= (*p == 0xDE50DEADBEEF0001ULL);
    ok &= (idt_pagefault_count() == pf0 + 1);
    uint64_t phys = vmm_get_phys(va);
    ok &= (phys != 0);
    if (phys != 0) {
        vmm_unmap(va);
        pmm_free_frame(phys);
    }

    kprintf("[B.5] selftest: %s (idtr=0x%016lx/%u bp=%lu pf=%lu)\n",
            ok ? "PASS" : "FAIL", idt_base(), (unsigned)idt_limit(),
            idt_breakpoint_count(), idt_pagefault_count());
    return ok ? 0 : 1;
}

static int test_pic(void) {
    int ok = 1;
    pic_clear_mask(0);                         // 只打开 IRQ0（PIT）
    uint8_t imr1 = pic_imr_master();
    uint8_t imr2 = pic_imr_slave();
    ok &= ((imr1 & 0x01) == 0 && (imr1 & 0xFE) == 0xFE);
    ok &= (imr2 == 0xFF);

    pit_init(1000);
    kprintf("[B.6] waiting for PIT IRQ0 with IF=1 ...\n");
    __asm__ volatile ("sti" : : : "memory");
    // hlt 只在中断到达时返回；上限 1000 次防止无中断时永久挂死。
    for (uint64_t i = 0; i < 1000 && pit_tick_count() < 3; i++) {
        __asm__ volatile ("hlt");
    }
    __asm__ volatile ("cli" : : : "memory");
    ok &= (pit_tick_count() >= 3);
    ok &= (idt_irq_count(0) >= 3);
    pic_set_mask(0);                           // 验完关掉，避免后续刷屏

    kprintf("[B.6] selftest: %s (imr=0x%x/0x%x irq0=%lu ticks=%lu)\n",
            ok ? "PASS" : "FAIL", (unsigned)imr1, (unsigned)imr2,
            idt_irq_count(0), pit_tick_count());
    return ok ? 0 : 1;
}

// ===== B.6b：切换到 APIC（在 B.6a 的 PIC 自检之后、B.7 调度之前）=====
static int test_apic(void) {
    int ok = 1;
    uint64_t irq0_before = idt_irq_count(0);
    int rc = apic_init();
    ok &= (rc == 0);
    ok &= (apic_enabled() == 1);
    ok &= (apic_lapic_phys() != 0 && apic_ioapic_phys() != 0);
    ok &= ((apic_lapic_version() & 0xFF) >= 0x10);
    ok &= (apic_ioapic_pins() > 0);
    ok &= (pic_imr_master() == 0xFF && pic_imr_slave() == 0xFF);  // PIC 已屏蔽

    // 读回真实的 IOAPIC 重定向表：PIT -> 向量 32、键盘 -> 向量 33，且未屏蔽。
    uint32_t gsi0 = apic_gsi_for_irq(0);
    ok &= (gsi0 < apic_ioapic_pins());
    ok &= ((apic_redir_low(0) & 0xFF) == 32);
    ok &= ((apic_redir_low(gsi0) & 0xFF) == 32);
    ok &= ((apic_redir_low(apic_gsi_for_irq(1)) & 0xFF) == 33);
    ok &= ((apic_redir_low(gsi0) & (1U << 16)) == 0);

    // 真实验收：APIC 模式下打开 IF，等 PIT 的 IRQ0 连续投递 3 次。
    // 用 pause 忙等而不是 hlt：中断配错时也能退出并给出 FAIL，不会挂死。
    if (rc == 0) {
        pit_init(1000);
        __asm__ volatile ("sti" : : : "memory");
        uint64_t spins = 0;
        while (idt_irq_count(0) < irq0_before + 3 && spins < 20000000) {
            __asm__ volatile ("pause");
            spins++;
        }
        __asm__ volatile ("cli" : : : "memory");
        ok &= (idt_irq_count(0) >= irq0_before + 3);   // 3 次证明 LAPIC EOI 生效
    }
    if (!ok) apic_fallback_to_pic();                   // 保住后续 B.7–B.11
    kprintf("[B.6b] selftest: %s (lapic=0x%016lx id=%u ver=0x%x ioapic=0x%016lx "
            "pins=%u gsi0=%u irq0+=%lu mode=%s)\n",
            ok ? "PASS" : "FAIL", apic_lapic_phys(), (unsigned)apic_bsp_id(),
            (unsigned)(apic_lapic_version() & 0xFF), apic_ioapic_phys(),
            (unsigned)apic_ioapic_pins(), (unsigned)gsi0,
            idt_irq_count(0) - irq0_before,
            apic_enabled() ? "APIC" : "PIC-fallback");
    return ok ? 0 : 1;
}

// ===== B.7 / B.8：抢占式调度 + 内核线程 =====
static volatile int a_steps, b_steps, a_done, b_done;

static void busy_wait(uint64_t n) {
    for (volatile uint64_t i = 0; i < n; i++) { }
}

static void worker_a(void *arg) {
    (void)arg;
    for (int i = 0; i < 3; i++) {
        __asm__ volatile ("cli" : : : "memory");   // 打印期间不被抢占，日志不交错
        a_steps++;
        kprintf("[B.7] worker A step=%d ticks=%lu\n", i + 1, timer_ticks());
        __asm__ volatile ("sti" : : : "memory");
        busy_wait(2500000);                        // 忙等期间会被 PIT 抢占
    }
    a_done = 1;
    thread_exit();
}

static void worker_b(void *arg) {
    (void)arg;
    for (int i = 0; i < 3; i++) {
        __asm__ volatile ("cli" : : : "memory");
        b_steps++;
        kprintf("[B.7] worker B step=%d ticks=%lu\n", i + 1, timer_ticks());
        __asm__ volatile ("sti" : : : "memory");
        busy_wait(2500000);
    }
    b_done = 1;
    thread_exit();
}

static int test_sched_and_threads(void) {
    int ok = 1;
    a_steps = b_steps = a_done = b_done = 0;
    uint64_t switches0 = sched_switch_count();

    struct thread *ta = thread_create("workerA", worker_a, NULL);
    struct thread *tb = thread_create("workerB", worker_b, NULL);
    ok &= (ta != NULL && tb != NULL);
    ok &= (thread_count() == 2);
    if (ta && tb) ok &= (ta->state == THREAD_READY && tb->state == THREAD_READY);

    timer_init(100);
    sched_enable();                 // 到这里才允许抢占，避免影响 B.1–B.6 自检
    pic_clear_mask(0);
    kprintf("[B.7] preemptive scheduling enabled (PIT %lu Hz)\n", (uint64_t)timer_hz());
    __asm__ volatile ("sti" : : : "memory");

    // 主线程退化成 idle：PIT 中断唤醒 -> 调度切换；A/B 都退出后回到这里。
    uint64_t guard = 0;
    while ((!a_done || !b_done) && guard < 200000) {
        __asm__ volatile ("hlt");
        guard++;
    }
    __asm__ volatile ("cli" : : : "memory");
    pic_set_mask(0);

    ok &= (a_done && b_done);
    ok &= (a_steps == 3 && b_steps == 3);
    ok &= (sched_switch_count() >= switches0 + 4);
    ok &= (timer_ticks() > 0);

    int ok7 = (timer_ticks() > 0) && (sched_switch_count() >= switches0 + 4);
    int ok8 = (ta != NULL && tb != NULL) && a_done && b_done &&
              (a_steps == 3) && (b_steps == 3) && (thread_count() == 2);
    kprintf("[B.7] selftest: %s (ticks=%lu switches=%lu, time-slice preemption)\n",
            ok7 ? "PASS" : "FAIL", timer_ticks(), sched_switch_count());
    kprintf("[B.8] selftest: %s (threads=%lu A=%d B=%d, both exited)\n",
            ok8 ? "PASS" : "FAIL", thread_count(), a_steps, b_steps);
    return (ok && ok7 && ok8) ? 0 : 1;
}

// ===== B.9：系统调用 =====
static int test_syscall(void) {
    int ok = 1;
    uint64_t t = do_syscall(SYS_GET_TICKS, 0, 0, 0);
    ok &= (t == timer_ticks());
    int64_t pid = (int64_t)do_syscall(SYS_GETPID, 0, 0, 0);
    ok &= (pid == 0);                    // 自检跑在 main 线程上

    const char msg[] = "[B.9] sys_write via int 0x80 OK\n";
    uint64_t n = do_syscall(SYS_WRITE, 1, (uint64_t)msg, sizeof(msg) - 1);
    ok &= (n == sizeof(msg) - 1);

    uint64_t bad = do_syscall(999, 0, 0, 0);
    ok &= (bad == (uint64_t)-1);         // 未知调用号返回 -1

    kprintf("[B.9] selftest: %s (vector=0x80 ticks=%lu pid=%ld registered=%d)\n",
            ok ? "PASS" : "FAIL", t, (long)pid, SYS_MAX);
    return ok ? 0 : 1;
}

// ===== B.10：用户态 ELF 加载 + ring3 运行 =====
static int test_userland(void) {
    int ok = 1;
    uint64_t ring3_before = syscall_ring3_count();
    uint64_t exits_before = syscall_exit_count();

    struct user_image img;
    int rc = elf_load(user_elf_start,
                      (uint64_t)(user_elf_end - user_elf_start), &img);
    if (rc != 0) {
        kprintf("[B.10] selftest: FAIL (elf_load rc=%d)\n", rc);
        return 1;
    }
    ok &= (elf_loaded_segments() > 0);
    ok &= (img.pml4_phys != 0 && img.pml4_phys != vmm_current_pml4_phys());
    struct thread *ut = thread_create_user("hello", &img);
    if (ut == NULL) {
        kprintf("[B.10] selftest: FAIL (thread_create_user)\n");
        return 1;
    }

    pic_clear_mask(0);
    __asm__ volatile ("sti" : : : "memory");
    uint64_t guard = 0;
    while (ut->state != THREAD_DEAD && guard < 200000) {
        __asm__ volatile ("hlt");
        guard++;
    }
    __asm__ volatile ("cli" : : : "memory");
    pic_set_mask(0);

    ok &= (ut->state == THREAD_DEAD);
    ok &= (syscall_ring3_count() > ring3_before);
    ok &= (syscall_exit_count() > exits_before);
    kprintf("[B.10] selftest: %s (segments=%lu entry=0x%016lx ring3-calls=%lu)\n",
            ok ? "PASS" : "FAIL", elf_loaded_segments(), img.entry,
            syscall_ring3_count() - ring3_before);
    return ok ? 0 : 1;
}

// ===== B.11：最小 Shell =====
static int test_shell(void) {
    shell_init();
    uint64_t c0 = shell_command_count();
    uint64_t ring3_before = syscall_ring3_count();
    shell_inject("help\n");
    int n1 = shell_process_input();
    shell_inject("echo DesOS B11 works\n");
    int n2 = shell_process_input();
    shell_inject("mem\n");
    int n3 = shell_process_input();
    shell_inject("bogus\n");
    int n4 = shell_process_input();
    shell_inject("run\n");                 // B.11 / B.10 联动：Shell 拉起用户程序
    int n5 = shell_process_input();
    shell_inject("ticks");                 // 只填行缓冲，不提交
    kbd_inject_scancode(0x1C);             // PS/2 Enter：走 IRQ1 的同一映射链路
    int n6 = shell_process_input();

    int lines = n1 + n2 + n3 + n4 + n5 + n6;
    int ok = (n1 == 1 && n2 == 1 && n3 == 1 && n4 == 1 && n5 == 1 && n6 == 1);
    ok &= (shell_command_count() == c0 + 5);       // 未知命令不计入成功执行数
    ok &= (syscall_ring3_count() > ring3_before);  // run 确实进入了 ring3
    kprintf("[B.11] selftest: %s (commands=%lu lines=%d run-ring3=%lu)\n",
            ok ? "PASS" : "FAIL", shell_command_count() - c0, lines,
            syscall_ring3_count() - ring3_before);
    return ok ? 0 : 1;
}

int b_selftest_run(void) {
    int fails = 0;
    fails += test_pmm();
    fails += test_vmm();
    fails += test_heap();
    fails += test_gdt();
    fails += test_idt();
    fails += test_pic();
    fails += test_apic();
    fails += test_sched_and_threads();
    fails += test_syscall();
    fails += test_userland();
    fails += test_shell();
    kprintf("B-STAGE: %s (failed=%d)\n", fails == 0 ? "PASS" : "FAIL", fails);
    return fails;
}
