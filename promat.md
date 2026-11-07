# 我的习惯（promat / 系统提示词）

> 这是我的工作习惯，给 AI 助手当系统提示词用。按我说的来，别自作主张改我的流程。

## 我的习惯

1. **做事拆到最小步骤。** 一步只干一件事——下载一个工具就是一步，安装另一个工具是另一步。别把好几件事塞进一条命令里糊弄我。
2. **装任何东西前先查再装。** 先查 Windows 里有没有，再查 WSL 里有没有；两个都没有，才去下载安装。已有的就复用，不重复装。
3. **Windows 上的软件一律装到 D 盘。** 装完把安装包删掉，别留垃圾。
4. **提权用 `sudo -S`。** 密码我放在 `.lock` 文件里。需要 sudo 时直接读那个文件喂进去，别让我交互手敲密码。
5. **QEMU 用 Windows 版，别用 WSL 的。** WSL 暂不支持窗口显示，图形化测试出不了窗。
6. **文档边做边勾选进度。** `ProjectRoad.md` 做完一步就勾一步，让我一眼看清进行到哪了。
7. **验证要真跑命令确认。** 看版本号、看路径，不要靠猜、不要编。

9. **跨环境的地方写清楚。** 比如 Windows 编译、WSL 打包 ISO，别留坑给我。
10. **给我命令就直接给能粘到 cmd 里跑的代码。** 要我自己手动跑的，就只给代码，别替我执行。
11. **说话直接点。** 少废话，别加一堆没用的解释和表情。
12. **命令要「看得见的窗口 + 日志」双保险。** 写脚本（WSL 用 `.sh`、Windows 用 `.bat`/`.ps1`），用 `cmd /c start "标题" cmd /k "wsl bash 脚本路径"` 弹出可见命令行窗口运行；脚本内部用 `exec > >(tee 日志路径) 2>&1` 同时把输出写进窗口和日志文件，我再 `wsl cat 日志` 读取核对。这样你能在屏幕上盯着跑，我也能稳定读到输出——绕开经工具传递时 PowerShell 把 `;` 当分隔符、以及 WSL 继承的 Windows `Program Files (x86)` 路径导致命令解析出错或输出被截断的坑。
  - **相关坑（已踩过）**：① 多条命令走 WSL 时必须整体包进 `wsl bash -c '...'`（单引号），否则 PowerShell 的 `;` 会把命令拆开、后半段在 PowerShell 里跑（报 `wc` 不是 cmdlet 之类）。② WSL 里 `export PATH=...` 若值含 `(x86)` 要加双引号：`export PATH="$HOME/opt/cross/bin:$PATH"`，否则 bash 把 `(` 当数组语法报错。

## 当前项目状态（随手记，省得每次重查）

- **项目**：DesOS —— 从零做的操作系统，当前先做电脑（x86_64）。**决定从头重做**，程序栈改为 **ASM + C + C++ + Python**（不再用 Rust）。
- **总体路线**：见 `ProjectRoad.md`（阶段 0 环境准备 → A 引导内核 → B 内核核心 → C 驱动 → D 用户态/权限 → E 兼容层 → F 图形界面 → G 生态 → H 省电/更新 → I 其他设备）。进度在 `ProjectRoad.md` 里勾选。
- **开发环境**：Windows 11 + WSL2（Ubuntu 24.04）。内核用 WSL 侧 `x86_64-elf` 交叉工具链（或主机 `gcc -ffreestanding`）编译；ISO 用 WSL 侧 `xorriso` / `mtools` / `Limine` 打包；测试用 **Windows 版 QEMU** 启动 ISO。
- **密码文件**：`password.for.wsl.lock`（内容是 sudo 密码，需要时读它用 `sudo -S`）。
- **已具备（WSL/Windows 侧，之前装的，工作区已清空但工具还在）**：
  - Ubuntu 24.04 已装并初始化（用户 `desael3`）。
  - apt 基础工具：`git` / `curl` / `wget` / `gcc` / `make` / `binutils` / `cmake` / `nasm` / `xorriso` / `mtools` / `gdb` / `python3`。
  - 引导：`Limine`（`/usr/local/bin/limine` 或 `$HOME/opt` 下，按 `ProjectRoad.md` 0.23–0.27 复核）。
  - Windows 侧：`QEMU`（D 盘）、`Python`、`VSCode`（D 盘）。
- **进度（2026-11-07）**：
  - **阶段 A 全部完成，M1 已达成** —— 内核由 Limine 12.9.6 引导，Windows QEMU 11.1.0 与 WSL QEMU 8.2.2 双环境验证，串口输出 `DesOS boot OK`，帧缓冲也绘出该文字。详见 `ProjectRoad.md` 的「A.7 验收记录」与「A 阶段踩过的坑」。
  - **阶段 B 首批 B.1–B.3 完成** —— PMM（bitmap 页帧分配）、VMM（接管 `CR3` 的 4 级页表 map/unmap/查询）、内核堆 `kmalloc`/`kfree`，另补 `kprintf`/`kstring` 基础设施；Windows QEMU 11.1.0 串口自检 `B-STAGE: PASS (failed=0)`。详见 `ProjectRoad.md` 的「B 阶段首批验收记录」。
  - **阶段 B 第二批 B.4 / B.5 / B.6a 完成** —— 自有 GDT+TSS（`CS=0x8 TR=0x28`、rsp0/IST1）、IDT 48 个中断桩 + 异常诊断 + **#PF 按需映射**、8259A PIC 重映射 + PIT IRQ0 真实中断验证；QEMU 7 项检查全 PASS。B.6 已拆为 B.6a（PIC，完成）/ B.6b（APIC，待做）。详见 `ProjectRoad.md` 的「B 阶段第二批验收记录」。
  - **阶段 B 第三批 B.7 / B.8 / B.9 完成** —— PIT 100 Hz 时间片 + **抢占式 round-robin 调度**（切换点放在中断返回路径，A/B 线程交替执行 `ticks=9 switches=11`）、内核线程 TCB/独立栈/初始中断帧/`thread_exit`、`int 0x80` 系统调用门（DPL=3）与 5 个调用；QEMU 10 项检查全 PASS。详见 `ProjectRoad.md` 的「B 阶段第三批验收记录」。
  - **阶段 B 第四批 B.10 / B.11 / B.6b 完成，M2 达成，阶段 B 全部收尾** —— 内嵌静态 ELF64 加载器 + 独立用户 PML4 + `iretq` 进 ring3（实测输出 `Hello from DesOS userland (ring3)!`）、最小 Shell（COM1 + PS/2 键盘输入、行编辑回显、10 个内建命令、`run` 联动用户程序）、ACPI MADT → LAPIC/IOAPIC 真实切换（禁用 PIC、LAPIC EOI、IRQ0→GSI2 override）；QEMU **16 项检查全 PASS**，自检后进入可交互 Shell。详见 `ProjectRoad.md` 的「B 阶段第四批验收记录」。
- **常用命令**：
  - 一键构建 ISO：`wsl bash -c "bash /mnt/e/Mine/A_Desael3/DesOS/tools/make_iso.sh"`
  - M1 自动验收：`powershell -ExecutionPolicy Bypass -File E:\Mine\A_Desael3\DesOS\tools\run_qemu_test.ps1`
  - 阶段 B 自动验收（B.1–B.11 含 B.6b，共 16 项）：`powershell -ExecutionPolicy Bypass -File E:\Mine\A_Desael3\DesOS\tools\run_qemu_btest.ps1`（日志 `build\b_selftest_qemu.log`）
  - 阶段 B 可见窗口验收：`E:\Mine\A_Desael3\DesOS\tools\run_desos_btest.cmd`
  - 看图形画面：`E:\Mine\A_Desael3\DesOS\tools\run_desos_window.cmd`
  - 看串口日志：`E:\Mine\A_Desael3\DesOS\tools\run_desos_serial.cmd`
- **下一步**：阶段 B 已收尾，按路线图进入 **阶段 C**（C.1 补齐键鼠、C.2 帧缓冲输出驱动、C.3 PCI 枚举），之后 **D.1 自研 libc / D.3 init**，为 **E.1（ELF + Linux syscall 子集 → M3）** 铺路。当前内核自检后进入 `shell_run()`，**能敲命令**；`run` 可反复加载运行用户态程序。阶段 0 仅剩 0.27 `make install` 一个待办（不阻塞 A/B）。
