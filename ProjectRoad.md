# DesOS 项目路线图（ProjectRoad）

> 从零开始做一个操作系统。先做**电脑（x86_64）**，后续再扩展到手机/平板/手表等。
> 本文档是**路线图 + 行动清单**。每一步都拆到最小（一步只干一件小事），做完一步就把它前面的 `[ ]` 改成 `[x]`。
> 环境沿用：`Windows 11` + `WSL2 (Ubuntu 24.04)`。涉及跨环境的地方会单独标 `[Win]` 或 `[WSL]`。
> 习惯遵循 `promat.md`：装前先查、Windows 软件装 D 盘、提权读 `password.for.wsl.lock` 用 `sudo -S`、QEMU 用 Windows 版、验证看版本号。

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
- **M1**：能进内核并在 QEMU 里看到 DesOS 启动字样 ✅ **已达成**（阶段 A 全部完成，详见 A.7 验收记录）
- **M2**：内核能加载并运行一个用户态程序 ✅ **已达成**（B.10：静态 ELF64 加载 + 独立用户 PML4 + `iretq` 进 ring3 + `int 0x80` 系统调用；实测串口输出 `Hello from DesOS userland (ring3)!`，详见「B 阶段第四批验收记录」）
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

### A.7 验收记录（M1，2026-11-07）

- 权威环境 **Windows QEMU 11.1.0**：`RESULT: PASS`，串口完整链路为 Limine 菜单 3 秒倒计时 → `Loading executable \`boot():/kernel.elf\`` → `ELF entry point` / `Base revision: 1` / `Requests count: 1` → `DesOS boot OK`。
- 交叉验证 **WSL QEMU 8.2.2**：同样输出 `DesOS boot OK`。
- 图形窗口 `-display gtk` 实测可用（1024x768x32），画面为深蓝底 + `DesOS boot OK` 点阵字。
- 复现命令：`wsl bash -c "bash /mnt/e/Mine/A_Desael3/DesOS/tools/make_iso.sh"`，再跑 `powershell -File tools\run_qemu_test.ps1`。
- **仍不能交互**：内核打印后进入 `hlt` 循环，没有键盘/串口输入驱动、没有 shell。要「能敲命令」需先做 B.5/B.6（IDT/中断）、C.1（键盘）、B.11（最小 Shell）。

### A 阶段踩过的坑（排障备忘）

1. **配置语法（最主要）**：`=` 分隔符、`:DesOS` 菜单项、行内注释、CRLF 行尾——都会让 Limine 12.x 解析不到选项/条目，表现为停在菜单（默认 5 秒倒计时）、报 `Default entry is not valid`，根本不加载内核。判断依据：串口出现 `Booting automatically in 5...` 就是配置没解析。注意**选项名不区分大小写**（CONFIG.md 明确），改大小写不解决问题。
2. **`.limine_reqs` 段名**：触发 base revision 0 的废弃机制 → panic 且不引导。改名 + 升 base revision 1 解决。
3. **`.bss` 用 `NOLOAD`**：Limine 不映射该段，`call kernel_main` 压栈即 #PF → 三重错误。
4. **LMA = 更高半 VMA**：物理载入地址不合法。用 `AT(0x100000)` 修正。
5. **ISO 启动镜像截断**：`-boot-load-size 4` 只拷 2048 字节，SeaBIOS 加载不了 Limine；按实际大小设 56。
6. **排障方法**：`serial: yes` + `-nographic` 可把 Limine 引导日志/panic 打到串口；`-display none` + monitor `screendump` 抓帧缓冲转 PNG，可「读」出只显示在 VGA 上的错误。

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
- [x] **B.10** 用户态 ELF 加载器（M2 基础）。→ `kernel/inc/elf.h` + `kernel/src/elf.c`，用户程序 `user/hello.c` / `user/linker.ld` / `tools/build_user.py`（静态 ELF64，`ENTRY(_start)`，加载地址 `0x400000`，NASM `incbin` 内嵌进内核 `.rodata`）。加载器校验 ELF header、逐页复制 `PT_LOAD`、用 `vmm_create_user_pml4()`（只复制更高半 PML4 项）与 `vmm_map_user()` 建独立用户地址空间，另映射 4 页用户栈；`thread_create_user()` 构造 `cs=0x23 / ss=0x1B / rflags=0x202` 的初始中断帧，调度时切 `CR3` + 更新 `TSS.rsp0`，统一 `iretq` 降权到 ring3。用户程序用 `int 0x80` 的 `SYS_WRITE` 打印、`SYS_EXIT` 退出。实测 `ring3-calls=2`，输出 `Hello from DesOS userland (ring3)!` ✅
- [x] **B.11** 最小 Shell（内核态），能读命令、打印回显。→ `kernel/inc/shell.h` + `kernel/src/shell.c`，输入 `serial.c` 的 COM1 轮询 + `kbd.c` 的 PS/2 键盘 IRQ1（C.1 最小子集），统一进 256 字节环形缓冲；行编辑支持可打印字符回显、退格、回车执行；内建命令 `help / echo / mem / ticks / pid / threads / run / version / clear / halt`，其中 `run` 联动 B.10 真正拉起用户态程序。`shell_run()` 打开 PIT/IRQ1 后进入 `sti + hlt` 交互主循环 ✅

### B 阶段首批验收记录（B.1–B.3，2026-11-07）

- 自检代码 `kernel/src/b_selftest.c`：由 `kernel_main` 在 M1 帧缓冲画面之后依次跑 B.1（单页/连续页分配 + HHDM 写读 + 释放计数复原）、B.2（内核文本与 HHDM 反查 + 新映射写读 + 解映射）、B.3（多块写读隔离 + 释放复用 + 1 MiB 大分配触发堆增长），全部真分配/真写入/真读回/真释放，结果打到 COM1。
- 权威环境 **Windows QEMU 11.1.0**（`-m 512M -nographic -no-reboot`）：`RESULT: PASS`。串口链路 `Requests count: 3` → `DesOS boot OK` → `[B.1] usable=130679 pages (510 MiB)`（数值随内核镜像大小略变）→ `[B.2] kernel va 0xffffffff800027c6 -> phys 0x...` → `[B.2] map 0xffffc90000000000 -> ..., readback=0x00c0ffee12345678` → `[B.1]/[B.2]/[B.3] selftest: PASS` → `B-STAGE: PASS (failed=0)`（堆按需映射 1040 KiB）。
- 新增 `tools/run_qemu_btest.ps1`（逐项判定 M1 boot / B.1 / B.2 / B.3 / 总标记，日志 `build/b_selftest_qemu.log`）与可见窗口入口 `tools/run_desos_btest.cmd`；原 `tools/run_qemu_test.ps1` 的 M1 判定继续可用。
- 复现命令：`wsl bash -c "bash /mnt/e/Mine/A_Desael3/DesOS/tools/make_iso.sh"`，再跑 `powershell -ExecutionPolicy Bypass -File tools\run_qemu_btest.ps1`。
- **当时仍不能交互**：B.4–B.11 未做（GDT/TSS、IDT/异常、PIC/APIC、调度器、进程、系统调用、ELF 加载器、Shell）；键盘驱动属 C.1。后续进展见下节。

### B 阶段第二批验收记录（B.4 / B.5 / B.6a，2026-11-07）

- `tools/build_kernel.py` 改为汇编 `kernel/asm/*.asm` 全部文件；汇编产物加 `asm_` 前缀，避免与同名 C 源（如 `gdt.asm` / `gdt.c`）的目标文件互相覆盖。
- 实测 **Windows QEMU 11.1.0**：`RESULT: PASS`（7 项检查全过）。串口链路：
  `[B.4] GDT base=... limit=55, CS=0x8 TR=0x28, TSS rsp0=... IST1=...` →
  `[B.5] IDT base=... limit=4095, 48 stubs installed (#DF/#PF use IST1)` →
  `[B.6] PIC remapped IRQ0-15 -> vectors 32-47, all IRQs masked` →
  `[B.5] #BP handled ... (returning)` →
  `[B.5] #PF handled: cr2=0xffffc90000002000 err=0x2 -> demand-mapped` →
  `[B.6] PIT ch0 1000 Hz` → `[B.6] selftest: PASS (imr=0xfe/0xff irq0=3 ticks=3)` →
  `[B.1]–[B.6] selftest: PASS` → `B-STAGE: PASS (failed=0)`。
- 验收脚本 `tools/run_qemu_btest.ps1` 检查项扩到 7 项（M1 + B.1–B.6 + 总标记），日志 `build/b_selftest_qemu.log`；可见窗口入口 `tools/run_desos_btest.cmd`。
- **B 阶段踩过的坑**：① NASM 的 `mov ds, ax` 实际编码为 `mov ds, eax`（32 位形式），必须先 `mov eax, edx` 清高 16 位，否则加载垃圾选择子 → #GP；② 长模式重载 CS 必须 `retfq`（`48 CB`），`retf`（`CB`）是 32 位远返回，只弹 32 位 RIP/CS；③ NASM 的 `ltr` 只接受 16 位寄存器写法（`ltr di`）；④ 同名 asm/C 源文件产物必须加前缀。
- **当时仍不能交互**：B.6b（APIC）、B.7–B.11 未做（定时器/调度器、进程、系统调用、ELF 加载器、Shell）；键盘驱动属 C.1。后续进展见下节。

### B 阶段第三批验收记录（B.7 / B.8 / B.9，2026-11-07）

- 本批优先做「通往 M2 的必经链路」，**B.6b（APIC）主动延后**：单核内核暂不需要，且不影响用户态程序加载。
- `idt_stubs.asm` 的公共入口在 `call isr_common_handler` 之后新增一个切换点：`mov rdi,rsp; call sched_switch_from_irq; mov rsp,rax`。这样所有线程被挂起时都停在同一形态的中断帧上，无需单独的上下文切换汇编，也不用在 PIC 中断处理中途 `iretq`。
- 实测 **Windows QEMU 11.1.0**：`RESULT: PASS`（10 项检查全过）。串口链路：
  `[B.6] PIT ch0 100 Hz` → `[B.9] syscall gate int 0x80 (vector 128, DPL=3), 5 calls registered` →
  `[B.7] preemptive scheduling enabled (PIT 100 Hz)` →
  `[B.7] worker A/B step=1..3` 交错打印（来回抢占，具体顺序随调度时序略变） →
  `[B.7] selftest: PASS (ticks=9~10 switches=11~12, time-slice preemption)` →
  `[B.8] selftest: PASS (threads=2 A=3 B=3, both exited)` →
  `[B.9] sys_write via int 0x80 OK` → `[B.9] selftest: PASS (vector=0x80 pid=0 registered=5)` →
  `B-STAGE: PASS (failed=0)`。
- `tools/run_qemu_btest.ps1` 检查项扩到 10 项，等待时间调到 20 秒（worker 忙等需要）。
- **仍不能交互**：B.6b（APIC）、B.10（用户态 ELF 加载器）、B.11（最小 Shell）未做；键盘/串口输入属 C.1。要真正「能敲命令」还需 B.10 + B.11 + C.1。（**本条为当时状态，已在第四批解决**）

### B 阶段第四批验收记录（B.10 / B.11 / B.6b，2026-11-07）

- 本批把 B 阶段剩余项一次做完：**B.10 用户态 ELF/ring3**、**B.11 可交互 Shell**、此前主动延后的 **B.6b APIC**。**M2 达成，阶段 B 全部完成**。
- 实测 **Windows QEMU 11.1.0**（`-m 512M -nographic -no-reboot`）：`RESULT: PASS - B.1-B.11 (incl. B.6b APIC) all self-tests passed`，16 项检查全过。串口关键链路：
  `Requests count: 4`（新增 RSDP 请求） →
  `[B.6b] MADT: lapic=0xfee00000 ioapic=0xfec00000 bsp=0 ver=0x14 pins=24 overrides=5 flags=0x1 gsi0=2, PIC disabled` →
  `[B.6b] selftest: PASS (... irq0+=3 mode=APIC)` →
  `[B.7] selftest: PASS (ticks=15 switches=17)`（APIC 模式下抢占依旧生效） →
  `[B.10] ELF loaded: entry=0x40003b rsp=0x7ffffffffff8 segments=1` →
  `Hello from DesOS userland (ring3)!` → `[B.10] selftest: PASS (ring3-calls=2)` →
  `DesOS> help / echo / mem / bogus / run / ticks` 的回显与执行 → 第二次 ring3 输出 →
  `[B.11] selftest: PASS (commands=5 lines=6 run-ring3=2)` → `B-STAGE: PASS (failed=0)` →
  `[B.11] interactive shell: COM1 serial + PS/2 keyboard (IRQ1)`。
- 验收脚本 `tools/run_qemu_btest.ps1` 检查项扩到 16 项（新增 B.6b、B.10、ring3 实输出、B.11、交互循环），等待时间 20 秒；`tools/run_desos_btest.cmd` 为可见窗口入口。
- **B.6b 是真实切换，不是只解析 ACPI 就打 PASS**：自检读回 IOAPIC 重定向表（PIT→向量 32、键盘→33、未屏蔽）、确认 `IMR=0xff/0xff`（PIC 已屏蔽），再 `sti` 让 PIT 经 IOAPIC 连续投递 3 次 IRQ0——若 LAPIC EOI 没写对，第 2/3 次不会到达。B.7–B.11 也全部在 APIC 模式下跑完。
- **B.11 输入链路**：自检用 `shell_inject()` 走完整「输入→回显→解析执行」，`run` 命令真拉起 B.10 用户程序（`run-ring3=2`），键盘路径用 `kbd_inject_scancode(0x1C)`（Enter）走与 IRQ1 handler 同一条 scancode 映射提交命令。真实按键需在有窗口/串口输入的 QEMU 里敲（`tools/run_desos_serial.cmd`），`-nographic` 自动验收无法注入按键。
- **本批踩过的坑**：① 用户栈 4 页若从 `0x7FFFFFFFD000` 起，第 4 页落进 `0x0000800000000000`（低半 canonical 上界之外 / PML4[256] 的 HHDM），`vmm_map_user` 返回 -4；整体下移一页到 `0x7FFFFFFFC000` 解决。② APIC 模式下 IRQ 结束必须写 **LAPIC EOI**，`pic_send_eoi()` 无效；`idt.c` 改成按 `apic_enabled()` 二选一。③ QEMU 的 MADT 有 `IRQ0 → GSI 2` override，只配 IOAPIC pin 0 收不到 PIT；同时配 pin 0 与 override 的 GSI。④ 伪中断向量用 `0x2F` 复用已安装的 IRQ15 桩，避免跳到空 IDT 项。⑤ `apic_fallback_to_pic()` 保证 APIC 验证失败时能退回 B.6a 的 PIC 路径，不让后续自检挂死。
- 复现命令：`wsl bash -c "bash /mnt/e/Mine/A_Desael3/DesOS/tools/make_iso.sh"`，再跑 `powershell -ExecutionPolicy Bypass -File E:\Mine\A_Desael3\DesOS\tools\run_qemu_btest.ps1`。
- **现在能交互**：自检结束后内核进入 `shell_run()`，串口/PS/2 键盘可敲命令；`run` 可反复加载运行用户态程序。C.1 的鼠标等仍待做。

---

## 阶段 C：设备与驱动

- [ ] **C.1** 键盘 / 鼠标（PS/2）。（键盘 IRQ1 + 主键区 scancode 映射的最小子集已随 B.11 实现；鼠标、修饰键、扩展键待做）
- [ ] **C.2** 帧缓冲输出（Limine 已给 framebuffer 地址，直接画点/字）。
- [ ] **C.3** PCI 枚举（找设备）。
- [ ] **C.4** 磁盘驱动：AHCI / ATA（能读扇区）。
- [ ] **C.5** 网络：e1000 / RTL8139 + 最小 TCP/IP 栈。
- [ ] **C.6** USB：EHCI / xHCI（键鼠/存储）。
- [ ] **C.7** ACPI 与电源管理（为笔记本省电打基础，见阶段 H）。

---

## 阶段 D：用户态、C 库与权限模型（目标 7）

- [ ] **D.1** 自研 `libc`（DesOS C 运行时）：基础 `stdio` / `stdlib` / `string` / 系统调用封装。
- [ ] **D.2** 权限分级（目标 7）：
      - `kernel-user`：内核自身，ring 0，最高权限。
      - `system-user`：系统服务/守护进程，受限特权，管硬件与系统资源。
      - `sudo-user`：管理员，可经授权提权（类似 sudo，需确认）。
      - `user`：普通用户，最小权限。
      - 在 VFS/进程/系统调用层做 capability / ACL 校验。
- [ ] **D.3** `init` 与服务管理（启动用户态、拉起系统服务）。
- [ ] **D.4** 自带 CLI（类 Linux，说明更清晰；用 Python 生成 `help` 文档与命令手册）。

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
| B | 内核核心 | ✅ **已完成**（B.1–B.11 全部完成，含 B.6b APIC 切换；QEMU 16 项检查 PASS；M2 达成，进入可交互 Shell）|
| C | 设备与驱动 | 待开始 |
| D | 用户态/权限 (M2) | 待开始 |
| E | 兼容层 (M3/M4/M6) | 待开始 |
| F | 图形界面 (M5) | 待开始 |
| G | 软件生态 | 待开始 |
| H | 省电/更新 | 待开始 |
| I | 其他设备 | 待开始 |
