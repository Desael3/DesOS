# DesOS

从零实现的 x86_64 操作系统。先做「电脑」，后续再扩展到手机 / 平板 / 手表。

> **当前状态**：阶段 A（M1）、B（M2）、C（设备与驱动）、D（用户态 / 权限 / CLI）**全部完成**，阶段 E（兼容层）待开始。内核由 Limine 12.9.6 引导，QEMU 全量自动验收全 PASS（`B-STAGE: PASS` / `C-STAGE: PASS`），VMware BIOS + UEFI 双固件冒烟通过，`poweroff`（ACPI S5）/ `reboot` 真机验证通过。**D.5 起默认静默启动**：自检、命令演示、`init` 汇报只写 COM1 串口（日志完整保留），屏幕开机只显示干净的欢迎画面与 `DesOS>` 提示符；`verbose on` 可随时打开屏幕日志镜像，`run` 可按名加载多个内嵌 ring3 程序。

## 文档索引

| 文件 | 内容 |
|------|------|
| `README.md` | 本文：项目介绍、环境、构建 / 运行 / 验证命令 |
| `ProjectRoad.md` | 工程树：目录结构、干了哪些 / 没干哪些、每一步的目的 |
| `log.md` | 各阶段 / 各步的验收证据与串口日志副本 |
| `NOTES.md` | 注意事项：踩过的坑、原因、解决办法、已知限制 |
| `docs/cli-commands.md` / `docs/cli-help.txt` | CLI 中文命令手册与纯文本帮助（由 `tools/gen_cli_docs.py` 自动生成，勿手改） |
| `promat.md` | 工作习惯与给 AI 助手的约束（提示词，不是项目文档） |
| `LICENSE` | 授权条款 |

## 目标

1. 兼容 / 大部分兼容 Windows `EXE` / `BAT` / `CMD` / `MSI`、Linux `DEB`、Android `APK` 及其安装后的软件运行。
2. 类似 Windows 但更圆润柔和的 UI。
3. 类似 Linux 的 CLI，但说明更清晰。
4. 硬件表现与其他系统一致。
5. 笔记本更省电。
6. 安全更新不打扰。
7. 类 Linux 权限分级：`user` / `sudo-user` / `system-user` / `kernel-user`。
8. 自有程序后缀，自动区分 `/` 与 `\` 路径（VFS 层统一归一化）。

完整目标、技术栈与兼容层设计见 `ProjectRoad.md` 的「0. 项目总览」。

## 技术栈

| 语言 | 负责 |
|------|------|
| ASM (NASM) | 引导桩、CPU 模式切换、GDT/IDT 早期设置、上下文切换、系统调用入口 |
| C | 内核核心：内存管理、中断、调度、驱动、兼容层核心 |
| C++ | 用户态框架、GUI 控件库、窗口管理器、包管理前端 |
| Python | 构建脚本、ISO 打包、文档生成、包管理器逻辑、自动化测试 |

## 环境要求

- **主机**：Windows 11 + WSL2 (Ubuntu 24.04)
- **WSL 侧**：`x86_64-elf` 交叉工具链（GCC 14.2.0，`~/opt/cross/bin`）、`nasm 2.16.01`、`xorriso 1.5.6`、`mtools 4.0.43`、`python3 3.12.3`、`binutils 2.42`、`make 4.3`、`gdb 15.1`
- **Windows 侧**：QEMU 11.1.0（`D:\Program Files\qemu\qemu-system-x86_64.exe`）、Git 2.53.0、Python 3.11.9、VSCode
- **引导**：Limine 12.9.6（BIOS + UEFI 混合 ISO）
- **可选**：VMware Workstation Pro 17.6.4（跨 VMM 冒烟测试，已验过）

> 交叉工具链与 Windows 软件的安装记录见 `ProjectRoad.md` 阶段 0；工具版本一律真跑命令验证过。

## 快速开始

在仓库根目录执行（PowerShell）：

```powershell
# 1. 用 WSL 侧工具链编译内核 + 打包 build/DesOS.iso
wsl bash -c "bash /mnt/e/Mine/A_Desael3/DesOS/tools/make_iso.sh"

# 2. 用 Windows 版 QEMU 跑全量自动验收（43 项串口检查 + 3 份 CLI 产物，约 30 秒）
powershell -ExecutionPolicy Bypass -File E:\Mine\A_Desael3\DesOS\tools\run_qemu_btest.ps1
```

构建链在编译前会自动跑 `tools/gen_cli_docs.py` 重新生成 CLI 文档，生成失败即中止构建。看到 `RESULT: PASS - B.1-B.11 + C.1-C.7 + D.1-D.5 all self-tests passed` 即通过。串口日志写到 `build/b_selftest_qemu.log`（每次跑脚本生成 / 覆盖，工作区整理后本地副本已清空）；默认静默启动不影响它，自检 / 命令演示 / `init` 汇报全程都在日志里。

其他入口：

| 用途 | 命令 |
|------|------|
| M1 自动验收 | `powershell -ExecutionPolicy Bypass -File E:\Mine\A_Desael3\DesOS\tools\run_qemu_test.ps1` |
| 全量可见窗口验收 | `E:\Mine\A_Desael3\DesOS\tools\run_desos_btest.cmd` |
| 看图形画面（`-display gtk`） | `E:\Mine\A_Desael3\DesOS\tools\run_desos_window.cmd` |
| 看串口交互（可敲命令） | `E:\Mine\A_Desael3\DesOS\tools\run_desos_serial.cmd` |

## 交互 Shell

自检结束后 `shell_run()` 会先重置行状态并打印首帧 `DesOS>`，串口与 PS/2 键盘（IRQ1）都能输入，无需先按任意键、也不吞第一个字符。C.1 起支持 Shift / CapsLock / Ctrl、方向键、Home / End / Delete 行编辑。

CLI 由 `tools/gen_cli_docs.py` 的 `COMMANDS` 表**单一数据源**生成（16 条命令 / 7 个别名 / 5 个分组），同时产出 `kernel/inc/cli_commands.h`（Shell 运行时读取）、`docs/cli-help.txt`、`docs/cli-commands.md`：

- **基础**：`help [command]` / `echo` / `clear` / `version` / `verbose [on|off]`
- **系统信息**：`mem` / `ticks` / `pid` / `threads` / `whoami` / `services`
- **用户程序**：`run [app] [priv]`
- **硬件调试**：`mouse`
- **电源**：`halt` / `poweroff` / `reboot`

`help <command>` 或任意命令加 `--help` / `-h` 显示详细帮助；别名 `?` / `uname` / `about` / `free` / `uptime` / `ps` / `svc` 会被规范化。`run` 从内嵌 manifest（`userapp_table[]`）按名加载 ELF，当前内置 `hello` 与 `init`，可指定 `user` / `sudo` / `system` / `kernel` 权限；`services` 与用户态 `svc_count()` / `svc_info()` 共用内核服务注册表。`verbose on|off` 切换屏幕日志镜像，串口日志始终完整。完整手册见 `docs/cli-commands.md`。

## 目录结构

```
DesOS/
├── limine.conf              # Limine 启动菜单配置（行尾必须 LF）
├── kernel/
│   ├── asm/                 # boot.asm / gdt.asm / idt_stubs.asm
│   ├── inc/                 # 31 个头文件（含 cli_commands.h 生成物、userapp.h、service.h）
│   ├── src/                 # 30 个 C 源文件（含 b_selftest.c、shell.c、init.c、xhci.c、fb.c）
│   └── kernel.ld            # 链接脚本
├── user/
│   ├── apps/                # hello.c / init.c（每个单独链接成内嵌 ELF）
│   ├── libc/                # 自研 libc：desos.h / stdio.c / stdlib.c / string.c
│   └── linker.ld            # ring3 加载地址 0x400000
├── docs/                    # cli-commands.md / cli-help.txt（自动生成，勿手改）
├── tools/                   # 构建脚本（make_iso.sh / build_*.py / gen_cli_docs.py）、
│   │                       #   QEMU / VMware 验收脚本、Limine 产物
├── build/                   # 构建产物与验收日志（.gitignore 忽略）
├── README.md
├── ProjectRoad.md
├── log.md
├── NOTES.md
├── promat.md
├── LICENSE
└── .gitignore
```

完整目录树与每一步的目的见 `ProjectRoad.md`。

## 里程碑

| 里程碑 | 含义 | 状态 |
|--------|------|------|
| M1 | 能进内核并在 QEMU 里看到 `DesOS boot OK` | 已达成（阶段 A） |
| M2 | 内核能加载并运行一个用户态程序 | 已达成（B.10，ring3） |
| M3 | 运行 Linux `DEB` 程序 | 未开始（E.1） |
| M4 | 运行 Windows `EXE` | 未开始（E.2） |
| M5 | 自带圆润图形界面可用 | 未开始（阶段 F） |
| M6 | 运行 `APK` | 长期攻坚（E.5） |

## 构建产物与日志

`build/` 被 `.gitignore` 忽略，本地保留、不入库。**工作区整理后（2026-11-08）这里只留 3 个文件**：

- **最终产物**：`kernel.elf`、`DesOS.iso`、`disk.img`（C.4 验收用的空白盘，脚本检测到不存在会自动创建）。这三者可直接启动 / 验收，无需先重建。
- **全部 `*.log` 已清理**：包括当前验收日志、9 个历史串口证据日志、3 个 C.7 冗余调试副本、C.6 xHCI 五轮迭代的 trace / 串口输出，以及 0 字节的 `xhci_err*.log`。各阶段的判定结果、串口片段与复现命令已逐条抄录在 `log.md` / `NOTES.md`，删掉本地副本不影响证据链完整性；两文件开头的说明已注明「正文里的 `build/*.log` 是当时的记录标识，不代表文件仍存在」。
- **已清理的其它内容**：`tools/__pycache__/`、空 `.err` 文件，以及全部可重建的编译中间产物（`*.o`、`user_image.asm`、`iso_root/`、`user/`）。构建脚本为全量重建（`build_iso.py` 每次 `rmtree` 重建 `iso_root`，`build_user.py` 用 `makedirs(exist_ok=True)`，所有 `.c`/`.asm` 每次全量重编），跑一次 `make_iso.sh` 即可完整恢复；`.gitignore` 已加 `__pycache__/` 与 `*.pyc` 防止缓存再生。
- **运行时会重新生成的文件**：`run_qemu_btest.ps1` 每次验收会重新写 `b_selftest_qemu.log`（串口输出）和 `b_selftest_qemu.err`（QEMU stderr，无错误时 0 字节）；这是运行产物，不是需要清理的垃圾，验收完可直接删。只想进一步瘦身时可删除 `disk.img`（16 MiB 空白验收盘，脚本会自动创建）。

## 已知限制

阶段 A–D 主线已完成，阶段 E（兼容层）待开始；`0.27 make install` 是阶段 0 唯一待办，不阻塞构建。D.2b 的 VFS ACL 依赖后续文件系统阶段尚未落地；`init` 目前是「监督 / 汇报」骨架，服务的动态 start/stop 需等 IPC + `SYS_SVC_CTRL`；`elf_load()` 每次加载的用户页 / PML4 尚未回收（阶段 E 内存回收一并处理）；`SYS_SVC_INFO` 的用户指针只做低半 canonical 范围校验（逐页 uaccess 待 VMM 审查）。帧缓冲只支持 32bpp、ASCII 位图字体（C 命令表与 `cli-help.txt` 走英文以避免屏幕乱码），画面靠显存读回自检验证；Up/Down/PgUp/PgDn/Insert 只识别不改行（无命令历史）。C.4 磁盘 / C.5 网络 / C.6 USB 在没有对应硬件的 VMware 环境会优雅跳过。详见 `ProjectRoad.md` 与 `NOTES.md`。

## 许可证

见 `LICENSE`。当前文件为「许可证待定」占位，选定协议后替换为全文。

## 注意事项

构建、引导、跨 VMM 验证过程中踩过的坑与解决办法集中在 `NOTES.md`，动手前建议先读。
