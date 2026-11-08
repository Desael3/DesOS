// shell.c —— B.11 最小 Shell（对应 ProjectRoad B.11）+ C.1b 行编辑

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
#include "mouse.h"
#include "acpi.h"
#include "priv.h"
#include "service.h"
#include "userapp.h"
#include "cli_commands.h"     // D.4：由 tools/gen_cli_docs.py 生成
#include <stdint.h>

#define INPUT_BUF 256
#define LINE_MAX  128

static volatile char input_buf[INPUT_BUF];
static volatile int input_head;
static volatile int input_tail;
static char line[LINE_MAX];
static int line_len;
static int cursor;
static int rendered_len;
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

// ===== D.4：CLI 帮助（数据源：tools/gen_cli_docs.py -> cli_commands.h） =====
// 屏幕上只输出 ASCII（帧缓冲位图字体限制）；中文详解见 docs/cli-commands.md。
static void cli_print_detail(const char *name) {
    for (unsigned i = 0; i < CLI_COMMAND_COUNT; i++) {
        const cli_command_t *e = &cli_commands[i];
        if (!streq(e->name, name)) continue;
        kprintf("usage: %s\n", e->usage);
        kprintf("alias: %s\n", e->aliases[0] != '\0' ? e->aliases : "(none)");
        kprintf("desc : %s\n", e->brief);
        kprintf("note : %s\n", e->details);
        kprintf("example: %s\n", e->example);
        return;
    }
    kprintf("help: no help for '%s'\n", name);
}

static void cli_print_list(void) {
    kprintf("DesOS CLI - command list (help <command> for details)\n");
    const char *last = "";
    for (unsigned i = 0; i < CLI_COMMAND_COUNT; i++) {
        const cli_command_t *e = &cli_commands[i];
        if (!streq(e->group, last)) {
            kprintf("[%s]\n", e->group);
            last = e->group;
        }
        kprintf("  %s : %s\n", e->usage, e->brief);
    }
}

static int cmd_help(const char *arg) {
    if (arg == NULL || arg[0] == '\0') {
        cli_print_list();
        return 0;
    }
    char name[24];
    int k = 0;
    while (arg[k] != '\0' && arg[k] != ' ' && k < (int)sizeof(name) - 1) {
        name[k] = arg[k];
        k++;
    }
    name[k] = '\0';
    cli_print_detail(cli_canonical(name));
    return 0;
}

// 串口/帧缓冲都没有 ANSI 光标控制：回行首重打整行，用空格擦掉旧尾部，
// 再用 \b 把光标移回 cursor。全部走 kprintf 以同时镜像到帧缓冲控制台，
// 配合 fb_console 对 \r（回到本行行首）/\b（左移并擦除）的语义即可原地重绘。
// C.1b 的插入/删除/移动都走这一条重绘路径。
static void redraw_line(void) {
    kprintf("\rDesOS> ");
    for (int i = 0; i < line_len; i++) kprintf("%c", line[i]);
    for (int i = line_len; i < rendered_len; i++) kprintf(" ");
    int back = (rendered_len > line_len ? rendered_len - line_len : 0)
             + (line_len - cursor);
    for (int i = 0; i < back; i++) kprintf("\b");
    rendered_len = line_len;
}

void shell_init(void) {
    input_head = input_tail = 0;
    line_len = cursor = rendered_len = 0;
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

void shell_input_flush(void) {
    input_head = input_tail = 0;
    line_len = cursor = rendered_len = 0;
}

void shell_inject(const char *s) {
    for (; s != NULL && *s != '\0'; s++) shell_input_push(*s);
}

// B.11 / B.10 联动命令：加载并运行内嵌的用户态 ELF 程序。
// 依赖 shell_run 已打开 PIT IRQ0 与中断；等待用户线程 SYS_EXIT 后返回。
// D.3b：`run [app] [priv]`——app 名来自 userapp manifest（默认 hello）；
// D.2：权限参数 user|system|sudo|kernel——控制台输入即 sudo 式授权
//（类比真实 sudo 需要口令确认；单用户控制台场景下操作者敲命令就是确认）。
static int cmd_run(const char *args) {
    char appbuf[16];
    const char *app = "hello";
    uint8_t priv = PRIV_USER;

    char tokb[2][16];
    const char *tokens[2] = { NULL, NULL };
    int ntok = 0;
    const char *p = (args != NULL) ? args : "";
    while (*p != '\0' && ntok < 2) {
        while (*p == ' ') p++;
        if (*p == '\0') break;
        int n = 0;
        while (*p != '\0' && *p != ' ' && n < 15) tokb[ntok][n++] = *p++;
        tokb[ntok][n] = '\0';
        tokens[ntok] = tokb[ntok];
        ntok++;
    }

    int pi = 0;
    if (ntok > 0 && userapp_find(tokens[0]) != NULL) {
        int k = 0;
        while (tokens[0][k] != '\0') { appbuf[k] = tokens[0][k]; k++; }
        appbuf[k] = '\0';
        app = appbuf;
        pi = 1;
    }
    if (ntok > pi && priv_parse(tokens[pi], &priv) != 0) {
        kprintf("run: unknown privilege '%s' (user|system|sudo|kernel)\n", tokens[pi]);
        return -1;
    }
    if (ntok > pi)
        kprintf("run: app=%s privilege=%s (authorized by console operator)\n",
                app, priv_name(priv));

    const struct userapp_entry *e = userapp_find(app);
    if (e == NULL || e->end <= e->start) {
        kprintf("run: app '%s' not found\n", app);
        return -1;
    }
    struct user_image img;
    if (elf_load(e->start, (uint64_t)(e->end - e->start), &img) != 0) {
        kprintf("run: ELF load failed\n");
        return -1;
    }
    struct thread *t = thread_create_user_priv(app, &img, priv);
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

    // D.4：别名规范化（uname -> version、free -> mem、svc -> services ...），
    // 分发链只需认规范名；别名表由生成器统一维护。
    const char *canon = cli_canonical(cmd);
    if (canon != cmd) {
        int k = 0;
        while (canon[k] != '\0' && k < (int)sizeof(cmd) - 1) {
            cmd[k] = canon[k];
            k++;
        }
        cmd[k] = '\0';
    }

    command_count++;
    if (streq(rest, "--help") || streq(rest, "-h")) {
        cli_print_detail(cmd);                 // 类 Linux：任意命令的 --help
        return 0;
    }
    if (streq(cmd, "help")) {
        cmd_help(rest);
    } else if (streq(cmd, "mouse")) {
        kprintf("mouse: x=%ld y=%ld buttons=%u packets=%lu irq12=%lu\n",
                (long)mouse_x(), (long)mouse_y(), (unsigned)mouse_buttons(),
                (uint64_t)mouse_packet_count(), (uint64_t)mouse_irq_count());
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
        kprintf("pid: %d (%s, priv=%s)\n", t ? t->id : -1, t ? t->name : "?",
                t ? priv_name(t->priv) : "?");
    } else if (streq(cmd, "whoami")) {
        struct thread *t = thread_current();
        kprintf("whoami: %s (pid=%d priv=%s)\n", t ? t->name : "?",
                t ? t->id : -1, t ? priv_name(t->priv) : "?");
    } else if (streq(cmd, "threads")) {
        kprintf("threads: created=%lu context-switches=%lu ring3-syscalls=%lu\n",
                thread_count(), sched_switch_count(), syscall_ring3_count());
    } else if (streq(cmd, "services")) {
        // D.3：服务表（与 SYS_SVC_INFO 用户态查询同一数据源）
        int n = service_count();
        int running = 0;
        for (int i = 0; i < n; i++) {
            char row[32];
            if (service_info(i, row, sizeof row) == 0) {
                kprintf("svc[%d] %s\n", i, row);
                int p = 0;
                while (row[p] != '\0' && row[p] != ' ') p++;
                p++;
                if (row[p] == 'r') running++;      // "running"
            }
        }
        kprintf("services: %d total, %d running\n", n, running);
    } else if (streq(cmd, "run")) {
        int rc = cmd_run(rest);
        if (rc != 0) kprintf("run: user program did not exit cleanly\n");
    } else if (streq(cmd, "version")) {
        kprintf("DesOS 0.1 x86_64 - stage D (libc / privilege / init / CLI)\n");
    } else if (streq(cmd, "verbose")) {
        // D.5：串口日志始终完整；只切换是否把 kprintf 镜像到帧缓冲屏幕
        if (streq(rest, "on")) kprintf_set_quiet(0);
        else if (streq(rest, "off")) kprintf_set_quiet(1);
        kprintf("verbose: screen mirror %s (serial log always on)\n",
                kprintf_is_quiet() ? "off" : "on");
    } else if (streq(cmd, "clear")) {
        kprintf("\n\n\n");
    } else if (streq(cmd, "halt")) {
        kprintf("halt\n");
        for (;;) __asm__ volatile ("cli; hlt");
    } else if (streq(cmd, "poweroff")) {
        // C.7：ACPI S5 关机（PM1a_CNT |= SLP_TYP<<10 | SLP_EN）。成功则不返回。
        kprintf("poweroff: ACPI S5 request...\n");
        if (acpi_poweroff() != 0)
            kprintf("poweroff: no ACPI PM (PM1a_CNT=0), halted instead\n");
        for (;;) __asm__ volatile ("cli; hlt");
    } else if (streq(cmd, "reboot")) {
        // C.7：优先 FADT RESET_REG，回退 8042（0xFE）。不返回。
        acpi_reboot();
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
        unsigned char uc = (unsigned char)c;

        if (c == '\r' || c == '\n') {
            kprintf("\n");          // 回车换行：串口与帧缓冲都换行（kputc 已为串口补 \r）
            line[line_len] = '\0';
            if (line_len > 0) {
                shell_exec_line(line);
                executed++;
            }
            line_len = cursor = rendered_len = 0;
            print_prompt();
        } else if (c == '\b' || uc == 0x7F) {
            if (cursor > 0) {
                for (int i = cursor - 1; i < line_len - 1; i++) line[i] = line[i + 1];
                line_len--;
                cursor--;
                redraw_line();
            }
        } else if (uc == KEY_LEFT) {
            if (cursor > 0) { cursor--; redraw_line(); }
        } else if (uc == KEY_RIGHT) {
            if (cursor < line_len) { cursor++; redraw_line(); }
        } else if (uc == KEY_HOME) {
            if (cursor != 0) { cursor = 0; redraw_line(); }
        } else if (uc == KEY_END) {
            if (cursor != line_len) { cursor = line_len; redraw_line(); }
        } else if (uc == KEY_DELETE) {
            if (cursor < line_len) {
                for (int i = cursor; i < line_len - 1; i++) line[i] = line[i + 1];
                line_len--;
                redraw_line();
            }
        } else if (uc == KEY_UP || uc == KEY_DOWN || uc == KEY_INSERT ||
                   uc == KEY_PGUP || uc == KEY_PGDN) {
            // 暂无命令历史 / 覆盖模式：C.1b 只识别，不改行
        } else if (c >= 32 && c < 127 && line_len < LINE_MAX - 1) {
            for (int i = line_len; i > cursor; i--) line[i] = line[i - 1];
            line[cursor] = c;
            line_len++;
            cursor++;
            redraw_line();
        }
    }
    return executed;
}

// D.5：自检阶段（B.11 命令演示、C.1 行编辑测试）会在 Shell 行缓冲里留下
// 脏的 rendered_len/line_len，且屏幕上那个 prompt 早被后续日志冲走。
// 若不重置：用户第一个按键触发 redraw_line() 时会按旧 rendered_len 补空格
// 擦尾，把刚输入的字符擦掉（实测"吞掉第一个字符"）；而且 shell_run 原本
// 不打印 prompt，只有按键触发 redraw 才出现 "\rDesOS> "，看起来像"必须
// 按任意键才切换行并显示提示符"。这里一次性解决：flush + 首帧 prompt。
void shell_prepare_interactive(void) {
    shell_input_flush();     // 输入环形缓冲 + line_len/cursor/rendered_len 全清
    kprintf("\n[B.11] interactive shell: COM1 serial + PS/2 keyboard (IRQ1)\n");
    print_prompt();
}

void shell_run(void) {
    shell_prepare_interactive();
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

int shell_line_length(void) { return line_len; }
int shell_cursor_pos(void)  { return cursor; }
const char *shell_line_text(void) {
    line[line_len] = '\0';
    return line;
}
