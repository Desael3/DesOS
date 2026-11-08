// init.c (kernel) —— D.3b：开机拉起用户态 init 进程（PRIV_SYSTEM）
#include "init.h"
#include "userapp.h"
#include "elf.h"
#include "thread.h"
#include "priv.h"
#include "pic.h"
#include "kprintf.h"
#include <stdint.h>
#include <stddef.h>   // NULL

void init_launch(void) {
    const struct userapp_entry *e = userapp_find("init");
    if (e == NULL || e->end <= e->start) {
        kprintf("[D.3] init: image not found in manifest\n");
        return;
    }
    struct user_image img;
    int rc = elf_load(e->start, (uint64_t)(e->end - e->start), &img);
    if (rc != 0) {
        kprintf("[D.3] init: ELF load failed rc=%d\n", rc);
        return;
    }
    struct thread *t = thread_create_user_priv("init", &img, PRIV_SYSTEM);
    if (t == NULL) {
        kprintf("[D.3] init: thread create failed\n");
        return;
    }
    // 打印期间关中断：否则 PIT 抢占切到 init 线程后，用户态 printf 会与
    // 这行 kprintf 在串口上交错成 "[Dinit: ..."（D.3b 首轮实测）。
    __asm__ volatile ("cli" : : : "memory");
    kprintf("[D.3] launching init (priv=system, pid=%d)\n", t->id);

    // 与 Shell cmd_run 相同：打开 PIT IRQ0 + 中断，hlt 等用户线程 SYS_EXIT
    pic_clear_mask(0);
    __asm__ volatile ("sti" : : : "memory");
    uint64_t guard = 0;
    while (t->state != THREAD_DEAD && guard < 200000) {
        __asm__ volatile ("hlt");
        guard++;
    }
    kprintf("[D.3] init: %s (waited=%lu)\n",
            (t->state == THREAD_DEAD) ? "exited" : "timeout", guard);
}