# DesOS 工程树（ProjectRoad）

> 从零开始做一个操作系统。先做**电脑（x86_64）**，后续再扩展到手机/平板/手表等。
> 本文档是**工程树 + 行动清单**：记录项目结构、**干了哪些 / 没干哪些**、**每一步的目的**与验收证据。
> **读法**：每条清单项就是一步（一步只干一件小事）；`[x]` = 已完成、`[ ]` = 未开始，条目正文是该步的目的与产出，`→` 后是落地文件或实测结果。做完一步就把 `[ ]` 改成 `[x]`。
> 环境沿用 `Windows 11` + `WSL2 (Ubuntu 24.04)`，跨环境处标 `[Win]` / `[WSL]`；工作习惯见 `promat.md`。

## 文档分工

| 文件 | 职责 |
|------|------|
| `README.md` | 项目介绍、环境要求、构建 / 运行 / 验证命令（先看这个） |
| `ProjectRoad.md` | 本文：工程树、进度、每步目的（验收证据已抽离到 `log.md`） |
| `log.md` | 各阶段 / 各步的验收证据与串口日志副本 |
| `NOTES.md` | 踩过的坑、原因、解决办法、已知限制 |
| `promat.md` | 工作习惯与 AI 助手约束（提示词） |
| `LICENSE` | 授权条款 |

## 项目目录树

```
DesOS/
├── limine.conf                  # Limine 12.x 启动菜单（行尾必须 LF）
├── kernel/
│   ├── asm/                     # boot.asm / gdt.asm / idt_stubs.asm
│   ├── inc/                     # 31 个 .h（pmm/vmm/heap/gdt/idt/pic/apic/sched/timer/
│   │                           #   service/priv/userapp/kprintf/fb/... + cli_commands.h 生成物）
│   ├── src/                     # 30 个 .c（含 b_selftest.c、shell.c、init.c、xhci.c、fb.c）
│   └── kernel.ld                # VMA 更高半 / LMA 0x100000
├── user/
│   ├── apps/                    # hello.c / init.c（每个单独链接成内嵌 ELF）
│   ├── libc/                    # 自研 libc：desos.h / stdio.c / stdlib.c / string.c
│   └── linker.ld                # ring3 加载地址 0x400000
├── docs/                        # cli-commands.md / cli-help.txt（gen_cli_docs.py 生成）
├── tools/
│   ├── limine/                  # build_limine.sh + 源码包 + 构建必需产物
│   ├── build_kernel.py          # NASM + x86_64-elf-gcc → build/kernel.elf（先跑 CLI 生成器）
│   ├── build_user.py            # user/apps/*.c → user_image.asm（NASM incbin + manifest）
│   ├── gen_cli_docs.py          # CLI 单一数据源 → cli_commands.h / cli-help.txt / cli-commands.md
│   ├── build_iso.py             # xorriso 打 BIOS/UEFI 混合 ISO
│   ├── make_iso.sh              # 一键构建入口
│   ├── build_cross_toolchain.sh # 交叉工具链构建
│   ├── run_qemu_test.ps1        # M1 自动验收
│   ├── run_qemu_btest.ps1       # 全量自动验收（43 项串口检查 + 3 份 CLI 产物）
│   └── run_desos_*.cmd          # 可见窗口 / 串口 / 全量测试入口
├── build/                       # 构建产物与验收日志（.gitignore 忽略）
├── README.md / ProjectRoad.md / log.md / NOTES.md / promat.md / LICENSE
└── .gitignore
```

## 阶段目的速览

| 阶段 | 目的 | 状态 |
|------|------|------|
| 0 环境准备 | 备齐 Windows + WSL2 工具链与 `x86_64-elf` 交叉编译器，使内核能编译、ISO 能打包、QEMU 能启动 | 进行中（仅 0.27 待办） |
| A 引导与内核骨架 | 让机器从 Limine 引导进内核，串口 + 帧缓冲能输出 `DesOS boot OK`（**M1**） | 已完成 |
| B 内核核心 | 建起 PMM/VMM/堆/GDT/IDT/PIC+APIC/调度/线程/syscall/ELF+ring3/Shell，能跑用户程序、能敲命令（**M2**） | 已完成 |
| C 设备与驱动 | 补全键鼠、帧缓冲输出、PCI 枚举，再到磁盘 / 网络 / USB / ACPI | ✅ 已完成（C.1–C.7 全完成，QEMU PASS + VMware 双固件冒烟 + `poweroff`/`reboot` 真机验证） |
| D 用户态与权限 | 自研 libc、四级权限、`init`、自带 CLI | ✅ 已完成（D.1–D.5 全完成；仅 D.2b VFS ACL 依赖后续文件系统阶段） |
| E 兼容层 | ELF + Linux syscall → `DEB`；PE + Win32 → `EXE`/`CMD`/`BAT`/`MSI`；ART → `APK` | 待开始（下一阶段主线） |
| F 图形界面 | framebuffer 合成器 + 圆润风格窗口管理器与控件库 | 待开始 |
| G 软件生态 | 自有程序 / 安装包格式、包管理器、仓库雏形 | 待开始 |
| H 省电与更新 | ACPI 电源管理进阶（调频 / 休眠）、不打扰的安全更新 | 待开始（C.7 已完成 ACPI S5 与重启基础） |
| I 其他设备 | 平板 / 手机（ARM64）/ 手表移植 | 远期 |

> **为什么按这个顺序**：0 先备工具链 → A 才能引导并输出 → B 先有 PMM/VMM/堆，才谈得上 GDT/IDT → 有中断才有 PIT/APIC 与抢占调度 → 有调度才有线程与 `int 0x80` → 分页 + 系统调用才能把 ELF 送进 ring3 → Shell 把前面所有能力收成可交互入口。C 之后是设备驱动，D/E 才谈用户态与兼容层。
> 踩坑与排障不在本文重复，统一见 `NOTES.md`；各步的判定结果与串口片段抄录在 `log.md`。`build/` 被 `.gitignore` 忽略，本地串口日志副本已全部清理（2026-11-08），跑一次验收脚本即可重新生成当前日志。

---

## 0. 项目总览

### 0.1 最终目标（用户视角，暂定）
1. 兼容 / 大部分兼容：
   - Windows：`EXE`、`BAT`、`CMD`、`MSI`
   - Android：`APK`
   - Linux：`DEB`
   - 以及上述安装包安装后的软件运行
2. 类似 Windows 但更**圆润/柔和**的 UI
3. 类似 Linux 的 CLI，但**说明更清晰**
4. 硬件表现与其他系统一致（主要看机器本身性能）
5. 笔记本更**省电**
6. 安全更新**不打扰**（不像 Windows 那么烦人）
7. 类似 Linux 的权限分级：`user` / `sudo-user` / `system-user` / `kernel-user`
8. 自有软件/程序后缀名，且**自动区分 `/` 和 `\` 路径**

### 0.2 程序栈与语言分工
| 语言 | 负责内容 |
|------|----------|
| **ASM (NASM)** | 引导启动桩、CPU 模式切换、GDT/IDT 早期设置、上下文切换、系统调用入口 |
| **C** | 内核核心：内存管理、中断、调度、驱动、兼容层核心（PE/ELF 加载、系统调用转发） |
| **C++** | 用户态框架、GUI 控件库、窗口管理器、包管理前端 |
| **Python** | 构建脚本、ISO 打包、帮助文档生成、包管理器逻辑、自动化测试 |

### 0.3 兼容层总体思路（最重要，先理解再动手）
- **DEB / APK 都最终跑在“类 Linux 环境”上** → 先做 **ELF 加载器 + Linux 系统调用子集**（这层同时也是 APK 的地基）。
- **EXE / BAT / CMD / MSI 跑在“类 Windows 环境”上** → 再做 **PE 加载器 + Win32 API 子集**（思路参考 Wine / ReactOS，但不直接复制代码）。
- **APK** = Android 运行时（ART）跑在上面的“类 Linux 环境”上 → 是最难的一项，列为长期攻坚。
- 路径规范化：系统内部统一用 `/`，输入时兼容 `\`，在 VFS 层做一次归一化。

### 0.4 里程碑（用来判断进度）
- **M1**：能进内核并在 QEMU 里看到 DesOS 启动字样 ✅ **已达成**（阶段 A 全部完成，详见 `log.md` 的「阶段 A」）
- **M2**：内核能加载并运行一个用户态程序 ✅ **已达成**（B.10：静态 ELF64 加载 + 独立用户 PML4 + `iretq` 进 ring3 + `int 0x80` 系统调用；实测串口输出 `Hello from DesOS userland (ring3)!`，详见 `log.md` 的「阶段 B」）
- **M3**：能运行一个 Linux `DEB` 程序（经兼容层）
- **M4**：能运行一个 Windows `EXE`（经兼容层）
- **M5**：自带圆润图形界面可用
- **M6**：能运行 `APK`（长期）

---

## 阶段 0：环境准备（工具下载与安装）

> 原则：每一步只做一件最小的事。能复用已有的就不重新装。验证一律看版本号。

- [x] **0.1 [Win]** 确认 Windows 已装 Git。→ `git version 2.53.0.windows.1` ✅
- [x] **0.2 [Win]** 确认 WSL2 + Ubuntu 24.04 已装。→ `Ubuntu-24.04 Running 2` ✅
- [x] **0.3 [WSL]** 更新软件源。`sudo apt-get update` 已执行 ✅（列表已刷新）
- [x] **0.4 [WSL]** 升级已装包。`sudo apt-get upgrade -y` 已执行 ✅（`apt list --upgradable` = 0）
- [x] **0.5 [WSL]** 安装 `gcc`。→ `gcc 13.3.0` ✅
- [x] **0.6 [WSL]** 安装 `make`。→ `GNU Make 4.3` ✅
- [x] **0.7 [WSL]** 安装 `binutils`。→ `ld (GNU Binutils) 2.42` ✅
- [x] **0.8 [WSL]** 安装 `nasm`。→ `NASM 2.16.01` ✅
- [x] **0.9 [WSL]** 安装 `xorriso`。→ `xorriso 1.5.6` ✅
- [x] **0.10 [WSL]** 安装 `mtools`。→ `mtools 4.0.43` ✅
- [x] **0.11 [WSL]** 安装 `git`（WSL 侧）。→ `git 2.43.0` ✅
- [x] **0.12 [WSL]** 安装 `python3` + `pip`。→ `python3 3.12.3` / `pip 24.0` ✅
- [x] **0.13 [WSL]** 安装 `gdb`（配合 QEMU 远程调试）。→ `GNU gdb 15.1` ✅
- [x] **0.14 [WSL]** 安装 `build-essential`（一批构建依赖）。→ `Status: install ok installed` ✅
- [x] **0.15 [Win]** QEMU Windows 安装包已装（免下载）。验证：`qemu-system-x86_64 --version` → `QEMU 11.1.0` ✅
- [x] **0.16 [Win]** QEMU 已装在 D 盘（免下载）。✅
- [x] **0.17 [Win]** `D:\QEMU` 已在 PATH。→ 验证 `QEMU 11.1.0` ✅
- [x] **0.18 [Win]** Python Windows 安装包已装（免下载）。→ `Python 3.11.9` ✅
- [x] **0.19 [Win]** Python 已装并加入 PATH。✅
- [x] **0.20 [Win]** VSCode 安装包已装（免下载）。→ `1.144.0` ✅
- [x] **0.21 [Win]** VSCode 已装在 D 盘。✅
- [x] **0.22 [Win]** VSCode 扩展已装：`C/C++`、`x86 and x86_64 Assembly`、`Python`、`Remote - WSL` ✅
- [x] **0.23 [WSL]** 下载 Limine 引导源码（官方 tarball，用 `wget`）。→ `limine-12.9.6.tar.xz`（813824 字节，已校验为有效 XZ，含标准源码树）✅
- [x] **0.24 [WSL]** 解压 Limine 源码。→ `tools/limine/limine-12.9.6/` ✅
  - **工作目录清理后**该目录只保留构建必需文件：`bin/` 的 `limine` / `limine-bios.sys` / `limine-bios-cd.bin` / `limine-uefi-cd.bin` / `BOOTX64.EFI`，以及 `limine-protocol/include/limine.h`；完整源码仍在 `tools/limine/limine-12.9.6.tar.xz`，需要重编时先解压再跑 `tools/limine/build_limine.sh`（脚本会自动解压）。
- [x] **0.25 [WSL]** 配置 Limine：`./configure`。→ 默认会关 BIOS（`LIMINE_NO_BIOS`），已加 `--enable-bios --enable-bios-cd --enable-uefi-x86-64 --enable-uefi-cd`（项目 A.7 用 `qemu -cdrom` 需 BIOS）✅
- [x] **0.26 [WSL]** 编译 Limine：`make`。→ 生成 `bin/limine` 及引导文件（`limine-bios.sys` / `limine-bios-cd.bin` / `limine-uefi-cd.bin` 等）✅
- [ ] **0.27 [WSL]** 安装 Limine：`sudo -S make install` → 验证 `limine --version`
- [x] **0.28 [WSL]** 下载 `binutils` 源码。→ 工具链已就绪（`~/opt/cross/bin` 已有）✅
- [x] **0.29 [WSL]** 解压 `binutils` 源码。✅
- [x] **0.30 [WSL]** 配置 binutils 为 `x86_64-elf` 目标。✅
- [x] **0.31 [WSL]** 编译 binutils：`make`。✅
- [x] **0.32 [WSL]** 安装 binutils：`make install` → `x86_64-elf-ld --version` ✅
- [x] **0.33 [WSL]** 下载 `gcc` 源码（仅 C）。✅
- [x] **0.34 [WSL]** 解压 `gcc` 源码。✅
- [x] **0.35 [WSL]** 配置 gcc 为 `x86_64-elf`。✅
- [x] **0.36 [WSL]** 编译 gcc：`make all-gcc`。✅
- [x] **0.37 [WSL]** 安装 gcc：`make install-gcc` → `x86_64-elf-gcc --version` ✅（GCC 14.2.0 @ ~/opt/cross/bin）
- [x] **0.38** 建立项目目录骨架：`kernel/asm`、`kernel/src`、`kernel/inc`、`tools/`、`docs/`。✅
- [x] **0.39** 编写 `.gitignore`。✅

> 备注：交叉工具链已完成（`x86_64-elf-gcc 14.2.0`）。后续内核即用此工具链编译，不再需主机 `gcc -ffreestanding` 顶替方案。

---

## 阶段 A：引导与内核骨架（目标 M1）

- [x] **A.1** 写 Limine 配置文件 `limine.conf`（设置启动项指向内核、framebuffer 分辨率）。→ 项目根 `limine.conf`。⚠️ **Limine 12.x 配置语法已改**：选项必须是 `option: value`（冒号，不是 `=`）、菜单项以 `/` 开头（不是 `:`）、注释必须单独成行、全局项须在首个菜单项之前、行尾必须 LF（选项名本身不区分大小写）。现为 `timeout: 3` / `verbose: yes` / `serial: yes` / `default_entry: 1` + `/DesOS` 条目（`protocol: limine`、`path: boot():/kernel.elf`、`resolution: 1024x768x32`）✅
- [x] **A.2 [ASM]** 写启动桩 `boot.asm`：建立栈、把控制权交给 `kernel_main`。→ `kernel/asm/boot.asm`（`extern kernel_main`，BSS 内 64KiB 更高半栈；`nasm -f elf64` 验证通过）。HHDM 由 Limine 开启，桩内无需设页表 ✅
- [x] **A.3 [C]** 写内核入口 `kernel_main.c`：串口 + 帧缓冲输出 `DesOS boot OK`。→ `kernel/src/kernel_main.c`（用帧缓冲 `red/green/blue_mask_shift` 拼像素色；5x7 极小点阵字体）。⚠️ **Limine 请求段不能叫 `.limine_reqs`**：base revision 0 下 Limine 会按已废弃机制把该名字的段解释为「请求指针数组」，触发 `PANIC: limine: .limine_reqs entry outside kernel image` 而拒绝引导。现用 `.limine_reqs_data` 段 + **base revision 1**（tag 放在 start/end 分隔符之间）✅
- [x] **A.4** 写链接脚本 `kernel.ld`（定义内核加载地址、段布局）。→ `kernel/kernel.ld`（`ENTRY(_start)`、VMA 基址 `0xFFFFFFFF80000000`、**`.text : AT(0x100000)` 让 LMA 落到低位物理地址**、`KEEP(.limine_reqs_data)`、`.bss` 并入可写 LOAD 段（**不能 `NOLOAD`**，否则压栈 #PF）、4K 对齐、`/DISCARD/` 丢无用段）。实测 `kernel.elf`：入口 `0xffffffff80000000`，2 个 LOAD 段（VMA 更高半 / LMA 低位），`memsz > filesz` ✅（注：C 须 `-mcmodel=kernel -mno-red-zone`，见 A.5）
- [x] **A.5** 写构建脚本（`tools/build_kernel.py` 或 `Makefile`）：NASM 编 asm + gcc 编 c → 链接成 `kernel.elf`。→ `tools/build_kernel.py`（nasm 编 `kernel/asm/boot.asm`，交叉 `x86_64-elf-gcc` 编 `kernel/src/*.c`，用 `kernel/kernel.ld` 链接；C 旗标含 `-mcmodel=kernel -mno-red-zone`；交叉链解析不到 PATH 时回退 `~/opt/cross/bin`）。产物 `build/kernel.elf` ✅
- [x] **A.6 [WSL]** 写 ISO 打包脚本。→ `tools/make_iso.sh`（调 `build_kernel.py` + `build_iso.py`）；`tools/build_iso.py` 用 `xorriso` 生成 BIOS/UEFI 混合 ISO、`-boot-load-size` 按 `limine-bios-cd.bin` 实际大小计算（56 扇区，避免启动镜像被截断）、`limine bios-install` 写引导代码，并把 `limine.conf` **统一转成 LF**（CRLF 会让选项值带上 `\r` 而失效）。产物 `build/DesOS.iso` ✅
- [x] **A.7 [Win]** 用 Windows 版 QEMU 启动 ISO。→ `tools/run_qemu_test.ps1`（`-nographic` 把 COM1 接到 stdout、自动判定 `DesOS boot OK`）。**QEMU 11.1.0 实测 PASS**：`limine: ELF entry point: 0xffffffff80000000` / `Base revision: 1` / `Requests count: 1` / `DesOS boot OK`；WSL QEMU 8.2.2 交叉验证同样通过；帧缓冲亦绘出 `DesOS boot OK`。看画面用 `tools/run_desos_window.cmd`（`-display gtk`），看串口日志用 `tools/run_desos_serial.cmd` ✅
- [x] **A.8 [C]** 加串口日志 `serial.c`（输出到 `COM1` 0x3F8，轮询 LSR 的 THR 空位）。→ `kernel/src/serial.c` + `kernel/inc/serial.h`；`serial_init()` 在 QEMU 环境下会卡在 LSR 等待，故 `kernel_main` 直接发（SeaBIOS 已设好 115200 8N1）✅

> **A 阶段验收记录**（M1）详见 `log.md` 的「阶段 A」。

> **A 阶段踩过的坑**（Limine 12.x 配置语法、`.limine_reqs` 段名、`.bss NOLOAD`、LMA、ISO 启动镜像截断、串口 / 帧缓冲排障方法）已汇总到 `NOTES.md` 第 1 节，本文不重复。

---

## 阶段 B：内核核心

- [x] **B.1** 物理内存管理：页帧分配器（bitmap / 链表）。→ `kernel/inc/pmm.h` + `kernel/src/pmm.c`。`kernel_main.c` 新增 `LIMINE_MEMMAP_REQUEST` / `LIMINE_HHDM_REQUEST`（请求数变 3）；只管理 `LIMINE_MEMMAP_USABLE` 页，bitmap 按「最大 usable 物理地址」定长、放进 usable 区并保留自身占用页与 pfn 0；提供 `pmm_alloc_frame(s)` / `pmm_free_frame(s)` / 统计接口。QEMU 512M 实测约 13 万 usable 页（510 MiB），bitmap 16 KiB ✅
- [x] **B.2** 虚拟内存：分页（4 级页表）、内核空间映射。→ `kernel/inc/vmm.h` + `kernel/src/vmm.c`。不推倒 Limine 已建好的页表，而是接管 `CR3` 指向的 PML4：经 HHDM 读写页表页，`vmm_get_phys` 支持 1GiB/2MiB 大页查询，按需向 PMM 取页建 PDPT/PD/PT 并 `vmm_map` / `vmm_unmap` + `invlpg`；内核更高半镜像与 HHDM 由 Limine 映射并验证可读。堆/自检虚拟区间划在空白 PML4 项 400/402（`0xFFFFC80000000000` / `0xFFFFC90000000000`），避开已有映射 ✅
- [x] **B.3** 内核堆：实现 `kmalloc` / `kfree`。→ `kernel/inc/heap.h` + `kernel/src/heap.c`（16 字节对齐、按虚拟地址有序的双向块链表、首次适配 + 拆分 + 相邻合并、`kcalloc`，上限 64 MiB；按需向 B.1 申请物理页、由 B.2 映射，失败回滚）。同期补基础设施：`kprintf`（串口格式化）、`kstring`（freestanding 的 `memset`/`memcpy` 等），`build_kernel.py` 加 `-fno-builtin` ✅
- [x] **B.4** 段表 GDT 与 TSS（用户态切换需要）。→ `kernel/inc/gdt.h` + `kernel/src/gdt.c` + `kernel/asm/gdt.asm`。替换 Limine 临时 GDT：7 项平坦段（kernel code/data、user data/code、64 位 TSS 描述符），`lgdt` 后用 **`retfq`（48 CB）** 远返回重载 `CS=0x08`、`ltr` 装 `TR=0x28`；TSS 带 `rsp0`（ring3→ring0 栈）与 `IST1`（#DF/#PF 专用栈）。用户态选择子先备好给 B.9/B.10。实测 `CS=0x8 TR=0x28` ✅
- [x] **B.5** 中断：IDT、异常向量、缺页处理。→ `kernel/inc/idt.h` + `kernel/src/idt.c` + `kernel/asm/idt_stubs.asm`（48 个向量桩统一栈帧：CPU 是否自动压错误码分开处理，公共入口保存 15 个通用寄存器后调 C 分发）。256 项 IDT，异常 0-31 带名字/错误码/RIP 诊断，`#DF`/`#PF` 走 IST1；**#PF 实现按需映射**：指定地址缺页时分配物理页 + `vmm_map`，`iretq` 后重试指令成功。`int3` 验证异常返回链路 ✅
- [x] **B.6a** 中断控制器（先 PIC）：8259A 重映射 IRQ0-15 → 向量 32-47、EOI、IMR 掩码。→ `kernel/inc/pic.h` + `kernel/src/pic.c`；PIT ch0 仅作验证中断源（1000 Hz）。自检 `sti` 后 `hlt` 等到 3 次 IRQ0（`imr=0xfe/0xff irq0=3`）✅
- [x] **B.6b** 切换到 APIC（LAPIC + IOAPIC，多核准备）。→ `kernel/inc/apic.h` + `kernel/src/apic.c`。`kernel_main.c` 新增 `LIMINE_RSDP_REQUEST`（请求数变 4）；解析 RSDP → XSDT/RSDT → MADT，取 LAPIC/IOAPIC 物理基址、`Interrupt Source Override`（QEMU 上 IRQ0 → GSI 2）、`PCAT_COMPAT` 标志；用 `vmm_map(..., VMM_PCD)` 把两块 MMIO 映到 PML4[401] 的专用高半地址；以 `IA32_APIC_BASE` MSR 打开 APIC、屏蔽全部 LVT、设 SVR（伪中断向量 0x2F 复用 IRQ15 桩）；IOAPIC 先屏蔽全部 24 个引脚，再把 PIT/键盘路由到向量 32/33（physical destination = BSP LAPIC ID）；最后屏蔽 8259A 并经 IMCR 把 INTR 切到 APIC。`idt.c` 的 IRQ 分支按 `apic_enabled()` 改发 LAPIC EOI。**切换后 B.7–B.11 全部在 APIC 模式下真实跑通**；验证失败会 `apic_fallback_to_pic()` 回退，不拖垮后续链路 ✅
- [x] **B.7** 定时器 + 调度器：先协作式，再抢占式（时间片）。→ `kernel/inc/timer.h` + `kernel/src/timer.c`（PIT tick 计数、时间片到期 `sched_request()`）、`kernel/inc/sched.h` + `kernel/src/sched.c`（round-robin 就绪队列）。**抢占切换点唯一放在 `isr_common` 的「handler 返回后、恢复寄存器前」**：`sched_switch_from_irq(cur_rsp)` 返回下一线程 rsp，两者栈布局完全一致，后续 `pop...iretq` 可直接复用；协作式让出走 `SYS_YIELD`。实测 PIT 100 Hz 下 A/B 线程按时间片交替执行 ✅
- [x] **B.8** 进程/线程模型：PCB/TCB、上下文结构。→ `kernel/inc/thread.h` + `kernel/src/thread.c`。`struct thread` 含 `rsp`/独立内核栈/`cr3`/状态/id/entry/arg；`thread_create()` 用 `kmalloc` 分配 16 KiB 栈，并在栈顶构造一份完整「中断帧」（`rip=thread_bootstrap`、IF=1、模拟 `call` 后的 rsp 对齐），使首次调度即可由统一 `iretq` 路径启动；`thread_exit()` 走 `SYS_EXIT` 标记 DEAD 并让出。当前所有线程共用内核 PML4，地址空间分离留到 B.10 ✅
- [x] **B.9** 系统调用接口：统一入口（asm 桩 → 分发表）。→ `kernel/inc/syscall.h` + `kernel/src/syscall.c` + `idt_stubs.asm` 的 `isr_stub_128`。用 `int 0x80`（IDT 向量 128、**DPL=3** 门，将来 ring3 也能调用），约定 `rax`=调用号、`rdi/rsi/rdx`=参数，返回值写回栈帧 `rax`；已注册 `SYS_WRITE` / `SYS_YIELD` / `SYS_EXIT` / `SYS_GET_TICKS` / `SYS_GETPID`。自检覆盖正常调用、未知调用号返回 -1 ✅
- [x] **B.10** 用户态 ELF 加载器（M2 基础）。→ `kernel/inc/elf.h` + `kernel/src/elf.c`，用户程序 `user/apps/hello.c`（B.10 时为 `user/hello.c`，D.3b 迁入 `apps/`）/ `user/linker.ld` / `tools/build_user.py`（静态 ELF64，`ENTRY(_start)`，加载地址 `0x400000`，NASM `incbin` 内嵌进内核 `.rodata`）。加载器校验 ELF header、逐页复制 `PT_LOAD`、用 `vmm_create_user_pml4()`（只复制更高半 PML4 项）与 `vmm_map_user()` 建独立用户地址空间，另映射 4 页用户栈；`thread_create_user()` 构造 `cs=0x23 / ss=0x1B / rflags=0x202` 的初始中断帧，调度时切 `CR3` + 更新 `TSS.rsp0`，统一 `iretq` 降权到 ring3。用户程序用 `int 0x80` 的 `SYS_WRITE` 打印、`SYS_EXIT` 退出。实测 `ring3-calls=2`，输出 `Hello from DesOS userland (ring3)!` ✅
- [x] **B.11** 最小 Shell（内核态），能读命令、打印回显。→ `kernel/inc/shell.h` + `kernel/src/shell.c`，输入 `serial.c` 的 COM1 轮询 + `kbd.c` 的 PS/2 键盘 IRQ1（C.1 最小子集），统一进 256 字节环形缓冲；行编辑支持可打印字符回显、退格、回车执行；内建命令 `help / echo / mem / ticks / pid / threads / run / version / clear / halt`，其中 `run` 联动 B.10 真正拉起用户态程序。`shell_run()` 打开 PIT/IRQ1 后进入 `sti + hlt` 交互主循环 ✅

> **B 阶段验收记录**（首批 B.1–B.3、第二批 B.4/B.5/B.6a）详见 `log.md` 的「阶段 B」。

> **B 阶段验收记录**（第三批 B.7/B.8/B.9、第四批 B.10/B.11/B.6b、B.7 修复）详见 `log.md` 的「阶段 B」。

---

## 阶段 C：设备与驱动

### 阶段 C 前置任务（2026-11-07 用户指示）

> P.1 / P.2 已完成；P.3 已完成（**C.1 键鼠、C.2 帧缓冲输出、C.3 PCI 枚举、C.4 磁盘、C.5 网络、C.6 USB、C.7 ACPI 全部完成，自检全 PASS**）。B.7 自检的修复已单独实施并复验（见 `log.md` 的「阶段 B」）。
>
> **验证策略（2026-11-07 用户指示，已固化为 `promat.md` 第 14 条）**：阶段 C 的每个小步骤只在 **Windows 版 QEMU** 上真跑验收；等**整个阶段 C 全部完成、自检全 PASS** 后，才在 VMware 上做一次 BIOS / UEFI 跨 VMM 冒烟测试。阶段 D 同理。不要每改一小步就开 VMware。

- [x] **P.1 [Win]** 安装 VMware。✅ **已完成（2026-11-07）**
      - **渠道排查**：`downloads.broadcom.com` 302 跳 `access.broadcom.com` OAuth 登录（与「不登录不注册」约束冲突）；`download3.vmware.com` 域名已下线；`softwareupdate.vmware.com/cds/vmw-desktop/` 官方 CDS 只剩 `info-only` 元数据（Broadcom 已移除安装包）；winget 源无 VMware 包；`web.archive.org` 本机不可达。最终走第三方 GitHub Releases 镜像 `201853910/VMwareWorkstation`，全程无需账号。
      - 安装包：`VMware-workstation-full-17.6.4-24832109.exe`（425426160 字节）。
      - **安全校验**（第三方渠道必做）：SHA-256 `10fe3a36f525d88aa133118ab3b5a16b18da88d4aa11b14d74e4164b3fb94ba9`，与 GitHub Release 公布的 `digest` 逐字节一致；Authenticode `Status=Valid`，签名者 `CN=Broadcom Inc`，签发者 DigiCert Trusted G4 Code Signing RSA4096 SHA384 2021 CA1，有效期至 2027-03-31。确认是 Broadcom 官方原版、未被篡改（仓库内另有 KeyGen，未下载、未使用；17.6.4 免费无需激活）。
      - 安装：静默 `/s /v"/qn EULAS_AGREED=1 INSTALLDIR=D:\VMware\Workstation AUTOSOFTWAREUPDATE=0"`，装到 D 盘。首次尝试额外加 MSI 日志开关 `/l*v`，bootstrapper 把 `EULAS_AGREED` 截成 `ULAS_AGREED`，`VM_ValidateEULAAccepted` 返回 3 → MSI 1603；去掉 `/l*v` 后成功。
      - 提权：本机 `ConsentPromptBehaviorAdmin=0` / `PromptOnSecureDesktop=0`（管理员免提示提升），`-Verb RunAs` 未弹任何登录、注册或确认窗口。用户随后允许改弹 UI 安装向导，但静默安装已完成且验证通过，故改为弹出 Workstation 主窗口供确认，未重跑安装向导（避免与已装产品冲突）。
      - 验证（真跑命令）：`vmware.exe` / `vmrun.exe` 均为 `17.6.4 build-24832109`；注册表卸载项 `VMware Workstation 17.6.4`；服务 `VMnetDHCP`、`VMware NAT Service` Running；主窗口 `MainWindowTitle = VMware Workstation` 已弹出。
      - 安装包与临时日志已按 `promat.md` 第 9 条删除。
- [x] **P.2 [Win]** 冒烟测试：用 VMware 启动 `build/DesOS.iso`，验证「引导 → `DesOS>` 提示符」链路。✅ **已完成（2026-11-07）**
      - VMX 配置：CD-ROM 指向 ISO、COM1 输出到文件（承接 `serial: yes`）；BIOS 与 UEFI 各验一次（详见 `log.md` 的「阶段 P」）。
      - 判定：Limine 引导日志 → `DesOS boot OK` → B.1–B.11 自检 → `B-STAGE: PASS` → Shell 可输入命令。
      - 键鼠：原担心 VMware 默认走 USB HID 而非 PS/2；实测 UEFI 模式下 PS/2 IRQ1 真实按键可用（详见 `log.md` 的「阶段 P」）。鼠标、修饰键、扩展键仍待 C.1。

> **VMware 验收记录**（P.2 BIOS、UEFI 补充、真实按键、B.7 修复摘要）详见 `log.md` 的「阶段 P」。

- [ ] **P.3** 开始阶段 C 编写：C.1 键鼠（拆 C.1a/b/c）→ C.2 帧缓冲输出 → C.3 PCI 枚举 → C.4 磁盘。前置：P.2 完成。→ **进行中，C.1 / C.2 / C.3 / C.4 已完成（2026-11-08）**。

> **VMware 硬化修复（2026-11-08）**：VMware 真机冒烟中发现的若干问题已修复并复验——C.1 键盘在 APIC 模式下 `kbd_init` 未真正打开 PS/2 端口/IRQ/扫描（`pic_clear_mask(1)` 在 APIC 下无效），已改为 8042 命令启用；C.3 自检原先硬要求 0300/00 VGA 设备（VMware 没有）判 FAIL，已改为仅信息项；Shell 回显在帧缓冲上因 `\r` 被忽略、`redraw_line` 只写串口而不写帧缓冲而失效（每键冒出 `DesOS>`），已修（帧缓冲支持 `\r` 回行首 + 重绘走 kprintf）；C.2 平滑滚动改为字符网格整屏重绘（消除逐像素撕裂）。以上均经 QEMU 验收 PASS。

### 阶段 C 清单

- [x] **C.1** 键盘 / 鼠标（PS/2）：✅ **已完成（2026-11-07，仅 QEMU 验收）**
  - [x] **C.1a** 键盘修饰键 + `0xE0` 扩展前缀识别（左右 Shift/Ctrl/Alt、Caps/Num/Scroll、Shift 上档符号、Ctrl+字母控制字符、扩展键码记录）。
  - [x] **C.1b** 扩展键行编辑（方向键 / Home / End / Delete 等接到 Shell 光标 / 插入 / 删除）。
  - [x] **C.1c** PS/2 鼠标（IRQ12 路由 + 8042 初始化 + 3 字节包解析 + `mouse` 命令）。
- [x] **C.2** 帧缓冲输出（Limine framebuffer）：✅ **已完成（2026-11-07，仅 QEMU 验收）** — 完整 5x7 ASCII 字库、像素/矩形/读回原语、`kprintf` 日志镜像到屏幕控制台（自动换行 + 滚动）。
- [x] **C.3** PCI 枚举（找设备）：✅ **已完成（2026-11-07，仅 QEMU 验收）** — 经典配置机制 #1（0xCF8/0xCFC）、枚举 bus 0–7、读 Vendor/Device/Class、按类找设备。
- [x] **C.4** 磁盘驱动（ATA PIO，主盘 0x1F0）：✅ **已完成（2026-11-08，QEMU 验收 PASS）** — `kernel/inc/ata.h` + `kernel/src/ata.c`：PCI 确认 IO 空间、8042/控制器软复位、IDENTIFY 取型号/容量、LBA28 读/写扇区；自检 `test_disk()` 读 LBA0 + 写回读（远扇区先备份后恢复）。QEMU `QEMU HARDDISK` 32768 扇区读写作证通过。VMware 等无传统 IDE 盘的环境优雅跳过（驱动已在 QEMU 验证）。⚠️ 根因曾为 `outb` 形参顺序写反（值/端口颠倒），导致命令从未送达 0x1F0。
- [x] **C.5** 网络（e1000）：✅ **已完成（2026-11-08，QEMU 验收 PASS）** — `kernel/inc/net.h` + `kernel/src/net.c`：PCI class 0x02,00 探测、BAR0 MMIO 映射（uncacheable）、软复位、RAL/RAH 读 MAC、SLU 链路 UP、TX/RX 描述符环初始化；自检 `test_net()` 验证 MAC 有效 + 链路 UP + TX 描述符被取走处理（QEMU `8086:100e` 验证通过）。⚠️ 描述符环用 PCD 非缓存映射以保证 DMA 回写可见；`TXD_STAT_DD` 回写仅在实际送达网络后端后才置位，故判据采用「DD 回写 **或** TDH 前进」。最小 TCP/IP 栈留待后续 C.5 子步。
- [x] **C.6** USB（xHCI + HID 键鼠）：✅ **已完成（2026-11-08，QEMU 验收 PASS）** — `kernel/inc/usb.h` + `kernel/src/xhci.c`：PCI class 0x0C,03,30 探测 qemu-xhci（`1b36:000d`）、64 位 BAR0 MMIO 映射（uncacheable）、HCRST、命令环/事件环/ERST/DCBAA 初始化并 Run；端口复位枚举 → Enable Slot → Address Device → EP0 控制传输读设备/配置描述符 → 识别 HID 接口 → Configure Endpoint 建中断 IN 端点 → boot 协议提交 standing 中断传输；`usb_poll()` 由 timer tick 非阻塞调用处理传输完成并解码 HID 键鼠报告，键入喂 `shell_input_push`、鼠标喂 `mouse_inject_rel`（与 PS/2 共享输入层）。自检 `test_usb()` 以「找到 xHCI 且枚举出设备（Address Device 全链路成功）」为判据，QEMU 枚举出 usb-kbd/usb-mouse 两个设备（`0627:0001`，高速，hid=2）验证通过。⚠️ 调试踩坑（均已解决）：① ERSTBA 指向事件环段表而非环本身；② runtime/doorbell 偏移不固定（QEMU=MMIO+0x1000/+0x2000，已实现运行时探测并回退 CAPLENGTH 布局）；③ Slot Context 的 RootHubPort 在 DW1 bits16-23（1-based）、ContextEntries 在 DW0 bits27-31，EP Type 在 EP 上下文 DW1 bits3-5；④ 输入控制上下文 DW0=Drop(0)/DW1=Add(0x3)；⑤ 命令 TRB 的 Slot ID 在控制字 bits24-31；⑥ **SeaBIOS 已枚举过 USB 设备且只 ep_disable 不释放 slot 归属，内核枚举前必须对全部 slot 发 Disable Slot 清残留**；⑦ 传输完成事件仅在 TRB 置 IOC(bit5) 时产生；⑧ setup TRB 参数为小端字节序：`DW0=wValue<<16|bRequest<<8|bmReqType`，`DW1=wIndex|wLength<<16`（曾写反致 QEMU 以 len 0 查询设备）。真实键鼠输入验证留待 VMware 冒烟。
- [x] **C.7** ACPI 与电源管理：✅ **已完成（2026-11-08，QEMU 验收 PASS + 真关机验证）** — `kernel/inc/acpi.h` + `kernel/src/acpi.c`：RSDP 校验（签名 + 全表 checksum，兼容 HHDM 地址/物理地址/EBDA 扫描兜底）→ XSDT/RSDT 逐表校验缓存 → `acpi_find_table()` 按签名查表；FADT 解析（SMI_CMD/ACPI_ENABLE、PM1a_CNT_BLK、ACPI 2.0+ 的 X_ 64 位字段与 RESET_REG）；DSDT 最小 AML 扫描取 `\_S5` 包的 SLP_TYP；`acpi_poweroff()` = 向 PM1a_CNT 写 `(SLP_TYP<<10)|SLP_EN`（QEMU 实测 pm1a=0x604、SLP_TYP=0，写后真断电）；`acpi_reboot()` = FADT RESET_REG 优先、8042(0xFE) 兜底。B.6b 的 RSDP→MADT 解析重构为复用本模块（apic.c 删除自有实现，`apic_set_rsdp` 移除、kernel_main 改调 `acpi_init`），B.6b 自检回归 PASS。Shell 新增 `poweroff` / `reboot` 命令；自检 `test_acpi()` 判据 = FACP/MADT/DSDT 三表齐 + PM1a_CNT 有效 + \_S5 解析成功。⚠️ 真关机验证方式：Shell 经 COM1 串口注入 `poweroff`（monitor `sendkey` 在 `-display none` 下进不了 guest，QEMU 侧问题），QEMU 进程随 S5 立即退出。⚠️ 踩坑：DSDT 不在 XSDT/RSDT 根表里（由 FADT 的 X_DSDT 指向）；`kprintf` 不支持 `%.6s`/`%llx`（参数错位会污染整行日志）。

> **C 阶段验收记录**（C.1a/b/c、C.2、C.3 仅 QEMU + **VMware 双固件（UEFI + BIOS）冒烟整体 PASS，reboot/poweroff 均真机验证，C.7 在 QEMU/UEFI/BIOS 三套 ACPI 环境（pm1a=0x604/0x444/0x1004）全部正确解析**，2026-11-08）详见 `log.md` 的「阶段 C」。**阶段 C 正式收官。**

---

## 阶段 D：用户态、C 库与权限模型（目标 7）

- [x] **D.1** 自研 `libc`（DesOS C 运行时）：✅ **已完成（2026-11-08，QEMU 验收 PASS）** — `user/libc/`（`desos.h` 公共头 + `string.c` / `stdio.c` / `stdlib.c`）：**共享 ABI 头 `kernel/inc/desos_abi.h`**（系统调用号唯一出处，内核 `syscall.h` 与 libc 经 `-I kernel/inc` 共引，杜绝两边定义漂移）；syscall 封装 `desos_syscall()`（int 0x80，rdi/rsi/rdx 三参数）+ `write`/`exit`/`yield`/`getpid`/`sys_ticks`；string 子集 `memcpy/memmove/memset/memcmp/strlen/strcmp/strncmp/strchr`；stdio 子集 `vsnprintf`（支持 `%[0][width]{l,ll,z}{d,i,u,x,X,s,c,p,%%}`，截断语义同 C 标准）+ `printf/vprintf/snprintf/puts/putchar`（无用户态缓冲，直通内核串口）；stdlib 子集 `atoi/abs`。构建改造 `tools/build_user.py`：多文件编译（`user/*.c` + `user/libc/*.c` → `build/user/obj/`）→ 按 `user/linker.ld` 链接单个静态 ELF → incbin 内嵌内核（`-fno-stack-protector` 防链接期 `__stack_chk_fail` 未定义）。`hello.c` 改用 libc（首行输出保持 `Hello from DesOS userland (ring3)!` 兼容 B.10 验收模式串）。验收：`RESULT: PASS`，libc demo 输出 `pid=3 ticks strlen strcmp` 全部正确。✅ 后续已跟进：ELF 加载器正确处理 `.bss`（memsz>filesz 零填充）；原先「所有段按 RWX 映射」的问题已在 **D.2c** 修复为按 `p_flags` 的 W^X 映射，见 D.2c 条目。
- [ ] **D.2** 权限分级（目标 7）：**进行中（D.2a 系统调用层已完成，2026-11-08，QEMU 验收 PASS）**
      - ✅ **D.2a（已完成）**：`kernel/inc/priv.h` + `kernel/src/priv.c` 四级权限模型（kernel/system/sudo/user，数值越小权限越高）+ capability 校验矩阵（`CAP_POWER`→sudo 起、`CAP_THREAD_CTRL`/`CAP_HW_ACCESS`→system 起，未知 capability 一律拒绝）；TCB 增加 `priv` 字段（内核线程=kernel、用户线程默认 user，`thread_create_user_priv()` 支持指定级）；新 syscall：`SYS_PRIV_INFO`（读当前权限级）+ `SYS_POWER_OP`（受 CAP_POWER 校验的关机/重启，req=2 仅校验模式供探测）；Shell `run [user|system|sudo|kernel]` 可带权限参数——单用户控制台场景下操作者敲命令即 sudo 式授权（打印 authorized by console operator）；自检 `test_priv()` 全矩阵校验。验收：`run`→`priv level=3 power-op: denied`，`run sudo`→`privilege=sudo` + `priv level=2 power-op: allowed`，`RESULT: PASS`。
      - [ ] D.2b：VFS 落地后在 VFS 层做 capability/ACL 校验（依赖文件系统）。
      - ✅ **D.2c W^X 段保护（已完成，2026-11-08，QEMU 验收 PASS）**：`elf.c` 按 PT_LOAD `p_flags` 映射（PF_X→可执行不置 NX、PF_W→可写、皆无→只读+NX；内容拷贝走 HHDM 不受用户 VA 权限影响），打印 `map=r-x` 等字段；用户栈改 RW-NX（栈不可执行，阻断栈上注入代码）；`vmm_init` 兜底启用 EFER.NXE（PTE NX 位需 NXE=1 才被 CPU 尊重）；新增 `vmm_get_pte_user()` 只读查询，`test_elf` 直接核对 PTE：text `NX=0,W=0`、stack `NX=1,W=1`（实测 text pte=0x25 / stack pte=0x80000000_00000067），B.10 自检 PASS。
- [x] **D.3** `init` 与服务管理（启动用户态、拉起系统服务）：✅ **已完成（D.3a + D.3b，2026-11-08，QEMU 验收 PASS）**
      - ✅ **D.3a（已完成）**：`kernel/inc/service.h` + `kernel/src/service.c` 服务注册表（`SVC_MAX=16` 条：name/priv/state）；7 个内置服务注册（`fb/kbd/mouse/acpi` running + `ata/net/usb` stopped，当前测试环境无对应硬件；D.3b 起由驱动按真实探测结果更新）；`SYS_SVC_INFO`（idx<0 返回总数，否则拷贝一条 `"name running kernel"` 文本行到用户缓冲，指针校验：低半 canonical 地址空间，正式逐页 uaccess 待 D.2b）；libc 封装 `svc_count()/svc_info()`；Shell `services` 命令；自检 `test_service()`（注册/计数/文本行/非法 idx/越界/缓冲过小全判据）。**验收抓到一个真 bug 并修复**：`svc_count()` 初版返回 -1——syscall 处理先校验用户缓冲再处理 idx<0 计数查询，而计数查询本不带缓冲；修复为 idx<0 先行。最终：内核表 7 服务、Shell 列表正确、用户态 `services: 7` + 逐行查询正确、`RESULT: PASS`。
      - ✅ **D.3b（已完成，2026-11-08，QEMU 验收 PASS）**：`user/apps/` 多程序（`hello.c` 迁入 + 新增 `init.c`）；`tools/build_user.py` 重写为每个 app 单独链接静态 ELF、NASM `incbin` 多个镜像并生成 manifest 表 `userapp_table[]`（name/start/end）+ `userapp_table_len`，hello 保留兼容符号 `user_elf_start/end` 供 B.10 自检；内核 `kernel/inc/userapp.h` + `kernel/src/userapp.c` 提供 `userapp_count/at/find`；`kernel/src/init.c` 的 `init_launch()` 在自检后以 **PRIV_SYSTEM** 创建 init 用户线程、开 PIT IRQ0 + sti 等待 `SYS_EXIT`（实测 `pid=7 priv=1`、读到 8 条服务、逐行汇报后正常退出）；Shell 升级为 `run [app] [priv]`（实测 `run init system` → `run: app=init privilege=system`）；自检 `test_userapp()` 校验 manifest 条目数、ELF 魔数、越界/未知名拒绝。修复两个问题：`svc_count()` 初版返回 -1（syscall 缓冲校验先于 idx<0 计数查询）；`launching init` 日志与用户 printf 串口交错（打印期间 cli 后再 sti）。
      - ⚠️ 已知限制：init 目前是「监督/汇报」骨架（服务 running 由内核启动、stopped 因无硬件跳过），真正的动态 start/stop 需等 IPC + `SYS_SVC_CTRL`；`elf_load()` 每次加载的用户页/PML4 尚未回收（B.10 遗留，阶段 E 内存回收一并处理）；`SYS_SVC_INFO` 的用户指针只做低半范围校验（逐页 uaccess 待 D.2b/VMM 审查）。

- [x] **D.4** 自带 CLI（类 Linux，说明更清晰；用 Python 生成 `help` 文档与命令手册）：✅ **已完成（2026-11-08，QEMU 验收 PASS）**
      - **单一数据源**：`tools/gen_cli_docs.py` 的 `COMMANDS` 规格表（16 条命令 / 7 别名 / 5 个分组，含 D.5 追加的 `verbose`；字段：name/group/usage/brief/details/example/aliases）；`validate()` 校验必填字段、组名合法、命令名与别名无冲突；`build_kernel.py` 每次构建前自动重生成，生成失败即中止构建，`run_qemu_btest.ps1` 也校验三份产物存在 → 文档与运行时永不漂移。
      - **三份产物**：`kernel/inc/cli_commands.h`（C 命令表 + `cli_canonical()` 别名解析，Shell 运行时读取）、`docs/cli-help.txt`（纯文本 help 文档，与 Shell 输出同格式）、`docs/cli-commands.md`（中文命令手册：约定 / 快速清单表格 / 逐命令详解+示例）。**中英双视图**：帧缓冲是 ASCII 位图字体，C 表与 help.txt 走英文映射（`EN` / `GROUP_EN`），中文简述与详解留给 Markdown 手册，避免屏幕乱码。
      - **Shell 升级**：`help` 改为按分组遍历生成表打印；新增 `help <command>` 详细帮助（usage/alias/desc/note/example）与任意命令 `--help` / `-h`；别名规范化（`uname`/`about`→version、`free`→mem、`uptime`→ticks、`ps`→threads、`svc`→services、`?`→help）；新增 `whoami`；`version` 文案更新到阶段 D。
      - **验收证据**：`RESULT: PASS`；新增 5 项检查全绿（D.4 help list / help detail / alias / whoami / --help）；B.11 自检注入序列扩充到 12 条成功命令仍 PASS；串口实测 `DesOS CLI - command list`、`usage: run [app] [priv]`、`DesOS 0.1 x86_64 - stage D`、`whoami: main (pid=0 priv=kernel)`、`usage: ticks`。

- [x] **D.5** 启动显示与交互修复（2026-11-08，用户反馈，QEMU 验收 PASS）：
      - **默认静默启动**：新增 `kprintf_set_quiet()` / `kprintf_is_quiet()`；从 `fb_console_init()` 之后到自检结束，kprintf 只写 COM1 串口（自检、命令演示、init 汇报全部保留在日志），帧缓冲屏幕不刷屏。进 Shell 前 `fb_clear(bg)` 重画 "DesOS boot OK" 大字（同时清掉 C.2 自检的像素测试痕迹）+ 新增 `fb_console_clear()` 清网格/行列状态，再恢复屏幕镜像。新增 `verbose [on|off]` 命令，可随时打开/关闭屏幕镜像（串口日志始终完整）；命令规格由 D.4 生成器统一维护。
      - **修复「吞掉第一个字符」**：根因是自检阶段（B.11 命令演示、C.1 行编辑测试）在 Shell 行缓冲留下脏 `rendered_len`，而 `shell_run()` 从未重置；用户第一个按键触发 `redraw_line()` 时按旧长度补空格擦尾，把刚输入的字符擦掉（VMware 日志里 `poweroff` 被打成 `ower...` 后删掉重打，正是此现象）。修复：新增 `shell_prepare_interactive()`（`shell_run` 与其共用）＝ `shell_input_flush()` 重置行状态 + 首帧 `print_prompt()`；另在 `kbd_init()` 开头排空 PS/2 输出缓冲，避免 IRQ1 打开前按下的键残字节被 "read configuration byte" 的 `inb(0x60)` 误读成 cfg（既吞键又可能写错控制器配置）。
      - **修复「必须按任意键才显示 `DesOS>`」**：`shell_run()` 原本不打印提示符，只有按键触发 `redraw_line()` 才输出 `\rDesOS> `；现在启动即打印首帧 prompt，与是否静默无关（verbose 模式下同样不再复现）。
      - **C.2 自检适配**：原判据「自检日志刷屏产生的 `fb_console_rows()>0`」在静默后失效，改为直接向 fb_console 写换行验证行数累计（`rows=1`）。
      - **回归自检**：`test_shell_startup()` 制造脏行状态 → `shell_input_flush()` 重置（与 `shell_prepare_interactive()` 的第一步一致，但不打印伪 banner，避免串口日志出现两次 `interactive shell`；首帧 prompt 与唯一一次 banner 由真正的 `shell_run()` 打印）→ 注入首字符 `'Z'` → 断言行缓冲恰为 `"Z"`（防吞字符回归）+ 校验 quiet 开关。验收：`RESULT: PASS`，新增 4 项 D.5 检查全绿（quiet boot / first key / first prompt / verbose）；B.11 注入序列扩到 13 条命令仍 PASS。


---

## 阶段 E：兼容层（重点、最难，目标 1）

- [ ] **E.1 [Linux]** ELF 加载器 + Linux 系统调用子集 → 运行 `DEB` 程序（目标 M3）。
- [ ] **E.2 [Windows]** PE 加载器 + Win32 API 子集 → 运行 `EXE`（目标 M4）。思路参考 Wine / ReactOS。
- [ ] **E.3 [Windows]** `CMD` / `BAT` 解释器。
- [ ] **E.4 [Windows]** `MSI` 安装包兼容（解析表、执行安装序列）。
- [ ] **E.5 [Android]** Android 运行时（ART）跑在 “类 Linux 环境” 上 → 运行 `APK`（目标 M6，长期攻坚）。

> 路径处理（目标 8）：VFS 层把 `\` 统一规范化为 `/`，对外同时接受两种写法。

---

## 阶段 F：图形界面（目标 2，圆润风格）

- [ ] **F.1** 合成器（compositor）：基于 framebuffer 的图层合成。
- [ ] **F.2** 窗口管理器：圆角、阴影、模糊、柔和平移（C++）。
- [ ] **F.3** 控件库（按钮/输入框/菜单等，C++）。
- [ ] **F.4** 原生应用格式 + 路径自动识别（见目标 8）。

---

## 阶段 G：自有软件生态（目标 8）

- [ ] **G.1** 自有程序/安装包后缀（如 `.dapp` 应用、`.dsys` 系统程序、`.dlib` 库）。
- [ ] **G.2** 包管理器（Python 写）：安装 / 卸载 / 查询 / 依赖。
- [ ] **G.3** 软件仓库 / 商店雏形。

---

## 阶段 H：省电与安全更新（目标 5、6）

- [ ] **H.1** 笔记本省电：CPU 调频、空闲节能、设备休眠（接阶段 C 的 ACPI）。
- [ ] **H.2** 安全更新机制：后台下载、定时安装、**不打扰**用户（不像 Windows 弹窗）。

---

## 阶段 I：扩展到其他设备（远期）

- [ ] **I.1** 平板：触控 + 现有内核移植。
- [ ] **I.2** 手机：ARM64 移植 + 移动 UI。
- [ ] **I.3** 手表：极小资源移植（裁剪内核与 UI）。

---

## 进度速查
| 阶段 | 内容 | 状态 |
|------|------|------|
| 0 | 环境准备 | 进行中（0.1-0.22 完成；0.23-0.26 完成，仅 0.27 `make install` 待办；交叉链已完成）|
| A | 引导与内核骨架 (M1) | ✅ **已完成**（A.1-A.8 全部完成，M1 达成；Windows QEMU 11.1.0 + WSL QEMU 8.2.2 双环境验证 PASS）|
| B | 内核核心 | ✅ **已完成**（B.1–B.11 全部完成，含 B.6b APIC 切换；M2 达成，进入可交互 Shell；B 阶段收官时 16 项检查 PASS，现已并入 43 项全量验收）|
| C | 设备与驱动 | ✅ **已完成**（C.1 键鼠、C.2 帧缓冲、C.3 PCI 枚举、C.4 磁盘、C.5 网络、C.6 USB、C.7 ACPI 全部完成，QEMU 验收 PASS + VMware 双固件冒烟 + `poweroff`/`reboot` 真机验证）|
| D | 用户态/权限 (M2) | ✅ **已完成**（D.1 libc、D.2a 权限分级+capability、D.2c W^X、D.3a 服务注册表、D.3b 多程序+真 init、D.4 CLI+文档生成、D.5 静默启动/首键修复全部完成，QEMU 43 项全量回归 PASS；仅 D.2b VFS ACL 依赖后续文件系统阶段）|
| E | 兼容层 (M3/M4/M6) | 待开始（下一阶段主线）|
| F | 图形界面 (M5) | 待开始 |
| G | 软件生态 | 待开始 |
| H | 省电/更新 | 待开始 |
| I | 其他设备 | 待开始 |
