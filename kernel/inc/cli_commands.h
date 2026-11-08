// 本文件由 tools/gen_cli_docs.py 自动生成，请勿手改。
#ifndef DESOS_CLI_COMMANDS_H
#define DESOS_CLI_COMMANDS_H

typedef struct {
    const char *name;
    const char *group;
    const char *usage;
    const char *brief;
    const char *details;
    const char *example;
    const char *aliases;
} cli_command_t;

typedef struct {
    const char *alias;
    const char *target;
} cli_alias_t;

static const cli_command_t cli_commands[] = {
    { "help", "Basics", "help [command]", "Show command list or detailed help", "No arg: list commands by group. With a name: print usage, aliases and notes. --help or -h after any command works too.", "help run", "?" },
    { "echo", "Basics", "echo [text...]", "Print a line of text", "Echoes arguments to serial and framebuffer console. Useful to check the input path.", "echo DesOS B11 works", "" },
    { "clear", "Basics", "clear", "Clear framebuffer console", "Clears screen history; serial log is not affected.", "clear", "" },
    { "version", "Basics", "version", "Show kernel version and stage", "Prints DesOS version, architecture and implementation stage.", "version", "uname, about" },
    { "verbose", "Basics", "verbose [on|off]", "Toggle screen log mirror (serial log always on)", "Default quiet boot: selftests go to COM1 only. 'on' mirrors kernel output to the framebuffer console again, 'off' restores quiet mode. No arg prints current state.", "verbose on", "" },
    { "mem", "System info", "mem", "Physical memory total and free", "Counts 4KiB pages and MiB from the B.1 PMM bitmap.", "mem", "free" },
    { "ticks", "System info", "ticks", "PIT ticks since boot", "PIT runs at 100 Hz by default; ticks / 100 is roughly uptime in seconds.", "ticks", "uptime" },
    { "pid", "System info", "pid", "Current thread id, name and privilege", "Main thread is always pid 0 with kernel privilege; run spawns separate threads.", "pid", "" },
    { "threads", "System info", "threads", "Thread stats: created / switches / ring3 syscalls", "Verifies B.7 preemptive scheduling and B.9 int 0x80 syscall gate.", "threads", "ps" },
    { "whoami", "System info", "whoami", "Show current identity (name, pid, privilege)", "Four levels: kernel / system / sudo / user (D.2 capability model).", "whoami", "" },
    { "services", "System info", "services", "List service registry (name, state, privilege)", "D.3 table: running = started by kernel, stopped = no matching hardware. Userland reads the same table via SYS_SVC_INFO.", "services", "svc" },
    { "run", "User programs", "run [app] [priv]", "Load and run an embedded user ELF program", "app comes from the build-time userapp manifest (hello, init), default hello. priv is user/system/sudo/kernel; console input is the sudo-style authorization. Privilege is stored in the thread TCB and checked against capabilities.", "run init system", "" },
    { "mouse", "Hardware debug", "mouse", "PS/2 mouse coords, buttons, packets, IRQ12 count", "Move the mouse and run again to see coords and packets grow (C.1c IRQ12 path).", "mouse", "" },
    { "halt", "Power", "halt", "Halt CPU (hlt loop, no power off)", "Enters a hlt loop; unlike poweroff, ACPI is not involved.", "halt", "" },
    { "poweroff", "Power", "poweroff", "ACPI S5 power off", "Parses PM1a_CNT from FADT and SLP_TYP from DSDT _S5, writes (SLP_TYP<<10)|SLP_EN. Port is 0x604 on QEMU, 0x444 on VMware UEFI, 0x1004 on VMware BIOS.", "poweroff", "" },
    { "reboot", "Power", "reboot", "Reboot the machine", "Uses FADT RESET_REG if available, otherwise the 8042 reset (0xFE).", "reboot", "" },
};
#define CLI_COMMAND_COUNT ((unsigned)(sizeof(cli_commands) / sizeof(cli_commands[0])))

static const cli_alias_t cli_aliases[] = {
    { "?", "help" },
    { "uname", "version" },
    { "about", "version" },
    { "free", "mem" },
    { "uptime", "ticks" },
    { "ps", "threads" },
    { "svc", "services" },
};
#define CLI_ALIAS_COUNT ((unsigned)(sizeof(cli_aliases) / sizeof(cli_aliases[0])))

// 别名 -> 规范命令名；非别名原样返回
static inline const char *cli_canonical(const char *name) {
    if (name == 0) return name;
    for (unsigned i = 0; i < CLI_ALIAS_COUNT; i++) {
        const char *a = cli_aliases[i].alias;
        const char *n = name;
        while (*a != 0 && *a == *n) { a++; n++; }
        if (*a == 0 && *n == 0) return cli_aliases[i].target;
    }
    return name;
}

#endif // DESOS_CLI_COMMANDS_H
