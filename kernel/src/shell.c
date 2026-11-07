// shell.c —— B.11 最小 Shell（对应 ProjectRoad B.11）

#include "shell.h"
#include "serial.h"
#include "kprintf.h"
#include "kbd.h"
#include "pic.h"
#include "pmm.h"
#include "vmm.h"
#include "timer.h"
#include "sched.h"
#include "thread.h"
#include "syscall.h"
#include "elf.h"
#include <stdint.h>

#define INPUT_BUF 256
#define LINE_MAX  128

static volatile char input_buf[INPUT_BUF];
static volatile int input_head;
static volatile int input_tail;
static char line[LINE_MAX];
static int line_len;
static uint64_t command_count;

static int streq(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static void print_prompt(void) {
    kprintf("DesOS> ");
}

void shell_init(void) {
    input_head = input_tail = 0;
    line_len = 0;
    command_count = 0;
    kprintf("[B.11] DesOS shell ready - type 'help' for commands\n");
    print_prompt();
}

void shell_input_push(char c) {
    int next = (input_head + 1) % INPUT_BUF;
    if (next == input_tail) return;          // 缓冲满，丢弃
    input_buf[input_head] = c;
    input_head = next;
}

void shell_inject(const char *s) {
    for (; s != NULL && *s != '\0'; s++) shell_input_push(*s);
}

// B.11 / B.10 联动命令：加载并运行内嵌的用户态 ELF 程序。
// 依赖 shell_run 已打开 PIT IRQ0 与中断；等待用户线程 SYS_EXIT 后返回。
static int cmd_run(void) {
    struct user_image img;
    if (elf_load(user_elf_start, (uint64_t)(user_elf_end - user_elf_start), &img) != 0) {
        kprintf("run: ELF load failed\n");
        return -1;
    }
    struct thread *t = thread_create_user("hello", &img);
    if (t == NULL) return -1;
    pic_clear_mask(0);
    __asm__ volatile ("sti" : : : "memory");
    uint64_t guard = 0;
    while (t->state != THREAD_DEAD && guard < 200000) {
        __asm__ volatile ("hlt");
        guard++;
    }
    // 保持中断开启，让 shell 主循环继续被 PIT 唤醒
    return (t->state == THREAD_DEAD) ? 0 : -1;
}

int shell_exec_line(const char *s) {
    char cmd[24];
    int i = 0;
    while (*s == ' ') s++;
    while (*s != '\0' && *s != ' ' && i < (int)sizeof(cmd) - 1) cmd[i++] = *s++;
    cmd[i] = '\0';
    while (*s == ' ') s++;
    const char *rest = s;
    if (cmd[0] == '\0') return -1;

    command_count++;
    if (streq(cmd, "help")) {
        kprintf("commands: help echo mem ticks pid threads run version clear halt\n");
    } else if (streq(cmd, "echo")) {
        kprintf("%s\n", rest);
    } else if (streq(cmd, "mem")) {
        kprintf("mem: total=%lu pages (%lu MiB), free=%lu pages\n",
                pmm_total_pages(), (pmm_total_pages() * PAGE_SIZE) >> 20,
                pmm_free_pages());
    } else if (streq(cmd, "ticks")) {
        kprintf("ticks: %lu (PIT %lu Hz)\n", timer_ticks(), (uint64_t)timer_hz());
    } else if (streq(cmd, "pid")) {
        struct thread *t = thread_current();
        kprintf("pid: %d (%s)\n", t ? t->id : -1, t ? t->name : "?");
    } else if (streq(cmd, "threads")) {
        kprintf("threads: created=%lu context-switches=%lu ring3-syscalls=%lu\n",
                thread_count(), sched_switch_count(), syscall_ring3_count());
    } else if (streq(cmd, "run")) {
        int rc = cmd_run();
        if (rc != 0) kprintf("run: user program did not exit cleanly\n");
    } else if (streq(cmd, "version") || streq(cmd, "about")) {
        kprintf("DesOS 0.1 x86_64 - stage B complete (M1/M2 reached)\n");
    } else if (streq(cmd, "clear")) {
        kprintf("\n\n\n");
    } else if (streq(cmd, "halt")) {
        kprintf("halt\n");
        for (;;) __asm__ volatile ("cli; hlt");
    } else {
        kprintf("%s: unknown command (try 'help')\n", cmd);
        command_count--;                     // 未识别的不算成功执行
    }
    return 0;
}

int shell_process_input(void) {
    int executed = 0;
    while (input_tail != input_head) {
        char c = input_buf[input_tail];
        input_tail = (input_tail + 1) % INPUT_BUF;
        if (c == '\r' || c == '\n') {
            serial_putc('\r');
            serial_putc('\n');
            line[line_len] = '\0';
            if (line_len > 0) {
                shell_exec_line(line);
                executed++;
            }
            line_len = 0;
            print_prompt();
        } else if (c == '\b' || c == 0x7F) {
            if (line_len > 0) {
                line_len--;
                serial_puts("\b \b");
            }
        } else if (c >= 32 && c < 127 && line_len < LINE_MAX - 1) {
            line[line_len++] = c;
            serial_putc(c);                  // 回显
        }
    }
    return executed;
}

void shell_run(void) {
    kprintf("\n[B.11] interactive shell: COM1 serial + PS/2 keyboard (IRQ1)\n");
    pic_clear_mask(0);       // PIT：让 hlt 周期性唤醒以轮询串口
    kbd_init();              // 打开 IRQ1
    __asm__ volatile ("sti" : : : "memory");
    for (;;) {
        while (serial_available()) shell_input_push(serial_getc());
        shell_process_input();
        __asm__ volatile ("hlt");
    }
}

uint64_t shell_command_count(void) { return command_count; }
