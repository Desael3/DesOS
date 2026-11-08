#!/usr/bin/env python3
"""gen_cli_docs.py - D.4 CLI 单一数据源生成器（对应 ProjectRoad D.4）

产出（每次构建自动重生成，禁止手改）：
  kernel/inc/cli_commands.h  Shell 运行时命令表（help / 别名解析）
  docs/cli-commands.md       类 Linux 命令手册
  docs/cli-help.txt          纯文本 help 文档（与 Shell help 输出同格式）

命令规格只在本文件 COMMANDS 里维护：新增/改名命令 -> 改这里 -> 重新构建。
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, "kernel", "inc", "cli_commands.h")
DOCS = os.path.join(ROOT, "docs")
MANUAL = os.path.join(DOCS, "cli-commands.md")
HELPTXT = os.path.join(DOCS, "cli-help.txt")

GROUPS = ["基础", "系统信息", "用户程序", "硬件调试", "电源"]

COMMANDS = [
    dict(name="help", group="基础", usage="help [command]",
         brief="显示命令清单，或某条命令的详细帮助", aliases=["?"],
         details="不带参数按分组列出全部命令；带命令名显示用法、别名与说明。任意命令后加 --help 或 -h 也可查看帮助。",
         example="help run"),
    dict(name="echo", group="基础", usage="echo [text...]",
         brief="回显一行文本", aliases=[],
         details="把参数原样输出到串口与帧缓冲控制台，常用于确认输入链路是否通畅。",
         example="echo DesOS B11 works"),
    dict(name="clear", group="基础", usage="clear",
         brief="清空帧缓冲控制台", aliases=[],
         details="清除屏幕上的历史输出，串口日志不受影响。", example="clear"),
    dict(name="version", group="基础", usage="version",
         brief="显示内核版本与阶段", aliases=["uname", "about"],
         details="打印 DesOS 版本、体系结构与当前实现阶段。", example="version"),
    dict(name="verbose", group="基础", usage="verbose [on|off]",
         brief="切换屏幕日志镜像（串口日志始终完整）", aliases=[],
         details="默认静默启动：自检与命令演示不刷屏，只写 COM1 日志。on 之后内核输出重新镜像到帧缓冲屏幕，off 恢复静默；不带参数显示当前状态。",
         example="verbose on"),

    dict(name="mem", group="系统信息", usage="mem",
         brief="物理内存总量与空闲量", aliases=["free"],
         details="单位为 4KiB 页与 MiB；数据来自 B.1 物理页管理器（PMM）位图。",
         example="mem"),
    dict(name="ticks", group="系统信息", usage="ticks",
         brief="内核启动以来的 PIT tick 数", aliases=["uptime"],
         details="PIT 频率默认 100 Hz，tick 数除以频率约等于开机秒数。", example="ticks"),
    dict(name="pid", group="系统信息", usage="pid",
         brief="当前主线程 id、名称与权限级", aliases=[],
         details="主线程恒为 pid 0（kernel 权限）；用户程序由 run 以独立线程运行。",
         example="pid"),
    dict(name="threads", group="系统信息", usage="threads",
         brief="线程统计：创建数 / 上下文切换 / ring3 系统调用数", aliases=["ps"],
         details="用于验证 B.7 抢占式调度与 B.9 int 0x80 系统调用门。",
         example="threads"),
    dict(name="whoami", group="系统信息", usage="whoami",
         brief="显示当前身份（线程名、pid、权限级）", aliases=[],
         details="权限级四级：kernel / system / sudo / user（D.2 capability 模型）。",
         example="whoami"),
    dict(name="services", group="系统信息", usage="services",
         brief="列出服务注册表（名字、状态、归属权限）", aliases=["svc"],
         details="D.3 服务表：running 表示内核已拉起，stopped 多因当前环境无对应硬件；用户态经 SYS_SVC_INFO 读同一张表。",
         example="services"),

    dict(name="run", group="用户程序", usage="run [app] [priv]",
         brief="加载并运行内嵌用户态 ELF 程序", aliases=[],
         details="app 名来自构建期 userapp manifest（当前 hello、init），缺省 hello。priv 可选 user/system/sudo/kernel；控制台敲命令即 sudo 式授权，权限记录在 TCB 上并受 capability 校验。",
         example="run init system"),

    dict(name="mouse", group="硬件调试", usage="mouse",
         brief="PS/2 鼠标坐标、按键、包与 IRQ12 计数", aliases=[],
         details="移动鼠标后再次执行可观察坐标与 packets 增长，验证 C.1c IRQ12 链路。",
         example="mouse"),

    dict(name="halt", group="电源", usage="halt",
         brief="停机（CPU hlt，不断电）", aliases=[],
         details="进入 hlt 循环；与 poweroff 不同，ACPI 不参与。", example="halt"),
    dict(name="poweroff", group="电源", usage="poweroff",
         brief="ACPI S5 关机", aliases=[],
         details="从 FADT 动态解析 PM1a_CNT 与 DSDT _S5 的 SLP_TYP，写 (SLP_TYP<<10)|SLP_EN 真断电；端口在 QEMU / VMware UEFI / VMware BIOS 分别为 0x604 / 0x444 / 0x1004。",
         example="poweroff"),
    dict(name="reboot", group="电源", usage="reboot",
         brief="重启整机", aliases=[],
         details="优先使用 FADT RESET_REG，缺失时回退 8042 键盘控制器 (0xFE)。",
         example="reboot"),
]
def validate():
    names = set()
    for c in COMMANDS:
        for key in ("name", "group", "usage", "brief", "details", "example"):
            if not c.get(key):
                raise ValueError("missing field " + key + " in " + c.get("name", "?"))
        if c["group"] not in GROUPS:
            raise ValueError("unknown group: " + c["group"])
        if c["name"] in names:
            raise ValueError("duplicate command: " + c["name"])
        names.add(c["name"])
        for a in c["aliases"]:
            if a in names:
                raise ValueError("alias collides: " + a)
            names.add(a)


def ordered():
    return [c for g in GROUPS for c in COMMANDS if c["group"] == g]


def cs(s):
    return json.dumps(s, ensure_ascii=False)


# Shell 帧缓冲是 ASCII 位图字体：C 表与 help.txt 只用英文；
# 中文简述/详解保留在 Markdown 手册（docs/cli-commands.md）。
GROUP_EN = {
    "基础": "Basics",
    "系统信息": "System info",
    "用户程序": "User programs",
    "硬件调试": "Hardware debug",
    "电源": "Power",
}

EN = {
    "help": ("Show command list or detailed help",
             "No arg: list commands by group. With a name: print usage, aliases and notes. --help or -h after any command works too."),
    "echo": ("Print a line of text",
             "Echoes arguments to serial and framebuffer console. Useful to check the input path."),
    "clear": ("Clear framebuffer console",
              "Clears screen history; serial log is not affected."),
    "version": ("Show kernel version and stage",
                "Prints DesOS version, architecture and implementation stage."),
    "verbose": ("Toggle screen log mirror (serial log always on)",
                "Default quiet boot: selftests go to COM1 only. 'on' mirrors kernel output to the framebuffer console again, 'off' restores quiet mode. No arg prints current state."),
    "mem": ("Physical memory total and free",
            "Counts 4KiB pages and MiB from the B.1 PMM bitmap."),
    "ticks": ("PIT ticks since boot",
              "PIT runs at 100 Hz by default; ticks / 100 is roughly uptime in seconds."),
    "pid": ("Current thread id, name and privilege",
            "Main thread is always pid 0 with kernel privilege; run spawns separate threads."),
    "threads": ("Thread stats: created / switches / ring3 syscalls",
                "Verifies B.7 preemptive scheduling and B.9 int 0x80 syscall gate."),
    "whoami": ("Show current identity (name, pid, privilege)",
               "Four levels: kernel / system / sudo / user (D.2 capability model)."),
    "services": ("List service registry (name, state, privilege)",
                 "D.3 table: running = started by kernel, stopped = no matching hardware. Userland reads the same table via SYS_SVC_INFO."),
    "run": ("Load and run an embedded user ELF program",
            "app comes from the build-time userapp manifest (hello, init), default hello. priv is user/system/sudo/kernel; console input is the sudo-style authorization. Privilege is stored in the thread TCB and checked against capabilities."),
    "mouse": ("PS/2 mouse coords, buttons, packets, IRQ12 count",
              "Move the mouse and run again to see coords and packets grow (C.1c IRQ12 path)."),
    "halt": ("Halt CPU (hlt loop, no power off)",
             "Enters a hlt loop; unlike poweroff, ACPI is not involved."),
    "poweroff": ("ACPI S5 power off",
                 "Parses PM1a_CNT from FADT and SLP_TYP from DSDT _S5, writes (SLP_TYP<<10)|SLP_EN. Port is 0x604 on QEMU, 0x444 on VMware UEFI, 0x1004 on VMware BIOS."),
    "reboot": ("Reboot the machine",
               "Uses FADT RESET_REG if available, otherwise the 8042 reset (0xFE)."),
}


def gen_header():
    lines = [
        "// 本文件由 tools/gen_cli_docs.py 自动生成，请勿手改。",
        "#ifndef DESOS_CLI_COMMANDS_H",
        "#define DESOS_CLI_COMMANDS_H",
        "",
        "typedef struct {",
        "    const char *name;",
        "    const char *group;",
        "    const char *usage;",
        "    const char *brief;",
        "    const char *details;",
        "    const char *example;",
        "    const char *aliases;",
        "} cli_command_t;",
        "",
        "typedef struct {",
        "    const char *alias;",
        "    const char *target;",
        "} cli_alias_t;",
        "",
        "static const cli_command_t cli_commands[] = {",
    ]
    for c in ordered():
        brief_en, details_en = EN[c["name"]]
        fields = (c["name"], GROUP_EN[c["group"]], c["usage"], brief_en,
                  details_en, c["example"], ", ".join(c["aliases"]))
        lines.append("    { " + ", ".join(cs(x) for x in fields) + " },")
    lines += [
        "};",
        "#define CLI_COMMAND_COUNT ((unsigned)(sizeof(cli_commands) / sizeof(cli_commands[0])))",
        "",
        "static const cli_alias_t cli_aliases[] = {",
    ]
    for c in ordered():
        for a in c["aliases"]:
            lines.append("    { " + cs(a) + ", " + cs(c["name"]) + " },")
    lines += [
        "};",
        "#define CLI_ALIAS_COUNT ((unsigned)(sizeof(cli_aliases) / sizeof(cli_aliases[0])))",
        "",
        "// 别名 -> 规范命令名；非别名原样返回",
        "static inline const char *cli_canonical(const char *name) {",
        "    if (name == 0) return name;",
        "    for (unsigned i = 0; i < CLI_ALIAS_COUNT; i++) {",
        "        const char *a = cli_aliases[i].alias;",
        "        const char *n = name;",
        "        while (*a != 0 && *a == *n) { a++; n++; }",
        "        if (*a == 0 && *n == 0) return cli_aliases[i].target;",
        "    }",
        "    return name;",
        "}",
        "",
        "#endif // DESOS_CLI_COMMANDS_H",
        "",
    ]
    return "\n".join(lines)
def help_lines():
    lines = ["DesOS CLI - command list (help <command> for details)", ""]
    for g in GROUPS:
        lines.append("[" + GROUP_EN[g] + "]")
        for c in ordered():
            if c["group"] == g:
                lines.append("  " + c["usage"] + " : " + EN[c["name"]][0])
        lines.append("")
    return lines


def gen_manual():
    lines = [
        "# DesOS CLI 命令手册",
        "",
        "> 由 `tools/gen_cli_docs.py` 从 `COMMANDS` 规格表自动生成，请勿手改。",
        "> 修改命令请改生成器后重新构建 ISO；`kernel/inc/cli_commands.h`、`docs/cli-help.txt` 同步更新。",
        "",
        "## 约定",
        "",
        "- 命令与参数用空格分隔，Enter 提交；支持 Backspace、方向键、Home/End/Delete（C.1b）。",
        "- 任意命令后加 `--help` 或 `-h` 可查看该命令帮助；`help <command>` 等价。",
        "- 别名由生成器统一维护，Shell 会自动规范化后再分发。",
        "- `run [priv]` 的提权属于单用户控制台授权（操作者敲命令即确认），受 D.2 capability 矩阵约束。",
        "",
        "## 快速清单",
        "",
    ]
    for g in GROUPS:
        lines += ["### " + g, "", "| 命令 | 用法 | 说明 | 别名 |", "|---|---|---|---|"]
        for c in ordered():
            if c["group"] == g:
                al = ", ".join("`" + a + "`" for a in c["aliases"]) or "-"
                lines.append("| `" + c["name"] + "` | `" + c["usage"] + "` | " +
                             c["brief"] + " | " + al + " |")
        lines.append("")
    lines += ["## 命令详解", ""]
    for c in ordered():
        al = ", ".join("`" + a + "`" for a in c["aliases"]) or "无"
        lines += [
            "### " + c["name"],
            "",
            "- 分组：" + c["group"],
            "- 用法：`" + c["usage"] + "`",
            "- 别名：" + al,
            "- 简述：" + c["brief"],
            "",
            c["details"],
            "",
            "示例：`" + c["example"] + "`",
            "",
        ]
    return "\n".join(lines) + "\n"


def main():
    validate()
    os.makedirs(DOCS, exist_ok=True)
    with open(HEADER, "w", encoding="utf-8", newline="\n") as f:
        f.write(gen_header())
    with open(MANUAL, "w", encoding="utf-8", newline="\n") as f:
        f.write(gen_manual())
    with open(HELPTXT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(help_lines()) + "\n")
    na = sum(len(c["aliases"]) for c in COMMANDS)
    print("OK: gen_cli_docs.py -> " + str(len(COMMANDS)) +
          " commands / " + str(na) + " aliases")
    print("    " + HEADER)
    print("    " + MANUAL)
    print("    " + HELPTXT)


if __name__ == "__main__":
    try:
        main()
    except ValueError as e:
        print("ERROR: " + str(e), file=sys.stderr)
        sys.exit(1)