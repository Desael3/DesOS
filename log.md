# DesOS 验收记录（log）

> 本文汇总 DesOS 各阶段、各步的**验收证据与判定结果**（实测串口链路、判定结果与复现命令）。
> 进度 / 勾选 / 每步目的见 `ProjectRoad.md`；踩坑根因与解决办法见 `NOTES.md`。
> **本文件按时间顺序保留当时的原始观测**：早期条目里「某阶段进行中 / 未完成」「16 项检查」「`RESULT: PASS - B.1-B.11 ...`」等均是该时点的真实记录，不作为当前状态；当前进度与验收项数以 `ProjectRoad.md` 进度速查和 `tools/run_qemu_btest.ps1` 为准。
> **本地串口日志副本已全部清理（2026-11-08）**：正文中出现的 `build/*.log` 路径是当时的记录标识与证据出处，不代表文件仍存在；判定依据以本文逐条抄录的串口片段、计数值与复现命令为准。需要重新取证时跑一次 `make_iso.sh` + `run_qemu_btest.ps1`，会重新生成当前验收日志 `build/b_selftest_qemu.log`；`build/` 下的产物被 `.gitignore` 忽略，本就不入库。

---

## 阶段 A：引导与内核骨架（M1，2026-11-07）

### A.7 验收记录（M1，2026-11-07）

- 权威环境 **Windows QEMU 11.1.0**：`RESULT: PASS`，串口完整链路为 Limine 菜单 3 秒倒计时 → `Loading executable \`boot():/kernel.elf\`` → `ELF entry point` / `Base revision: 1` / `Requests count: 1` → `DesOS boot OK`。
- 交叉验证 **WSL QEMU 8.2.2**：同样输出 `DesOS boot OK`。
- 图形窗口 `-display gtk` 实测可用（1024x768x32），画面为深蓝底 + `DesOS boot OK` 点阵字。
- 复现命令：`wsl bash -c "bash /mnt/e/Mine/A_Desael3/DesOS/tools/make_iso.sh"`，再跑 `powershell -File tools\run_qemu_test.ps1`。
- **仍不能交互**：内核打印后进入 `hlt` 循环，没有键盘/串口输入驱动、没有 shell。要「能敲命令」需先做 B.5/B.6（IDT/中断）、C.1（键盘）、B.11（最小 Shell）。

---

## 阶段 B：内核核心（M2，2026-11-07）

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
- **本批踩过的坑**（NASM 段寄存器编码、`retfq`、`ltr` 写法、同名 asm/C 产物覆盖）见 `NOTES.md` 第 2 节。
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
- **本批踩过的坑**（用户栈地址越界、LAPIC EOI、`IRQ0 → GSI 2` override、伪中断向量、APIC 回退）见 `NOTES.md` 第 3 节。
- 复现命令：`wsl bash -c "bash /mnt/e/Mine/A_Desael3/DesOS/tools/make_iso.sh"`，再跑 `powershell -ExecutionPolicy Bypass -File E:\Mine\A_Desael3\DesOS\tools\run_qemu_btest.ps1`。
- **现在能交互**：自检结束后内核进入 `shell_run()`，串口/PS/2 键盘可敲命令；`run` 可反复加载运行用户态程序。C.1 的鼠标等仍待做。

### B.7 修复记录（2026-11-07）

- **问题**：VMware UEFI 下 `B.7 selftest: FAIL (ticks=1 switches=3)` → `B-STAGE: FAIL (failed=1)`；QEMU 各固件均 PASS，引导与 Shell 正常。
- **结论**：测试脆弱性（flaky test），不是内核缺陷——`busy_wait(2500000)` 固定循环次数在硬件 VT-x 快 VCPU 上撑不过 1 个 10ms tick。四环境交叉验证、判据与根因推导见 `NOTES.md` 第 4 节。
- **修复**：仅改 `kernel/src/b_selftest.c`；忙等换成 `wait_for_ticks(2)`（按 PIT tick 计时，带 `B7_SPIN_GUARD` 防挂死），新增 `steps_alternated()` 校验 A/B 交替与日志 `alt=` 字段，PASS 才真正证明时间片抢占。
- **复验**：QEMU 16 项自动验收全 PASS；VMware UEFI 由 FAIL 转 PASS。两环境均为 `B.7 selftest: PASS (ticks=10 switches=12 alt=1)`、`B-STAGE: PASS (failed=0)`；UEFI 路径由 `vmware.log` 的 `EFI2.7 64-bit` / `About to do EFI boot` 佐证。
- **证据**：`build/b_selftest_qemu.log`、`build/vmware_b7_fixed_uefi_serial.log`（修复后）、`build/vmware_p2_uefi_serial.log`（修复前 FAIL）、`build/qemu_uefi_serial.log`（QEMU+OVMF）。

---

## 阶段 P：阶段 C 前置任务验收（VMware 跨 VMM 冒烟，2026-11-07）

### P.2 验收记录（VMware BIOS，2026-11-07）

- 环境：**VMware Workstation Pro 17.6.4 build-24832109**（Windows 11 主机，与 WSL2/Hyper-V 共存）。虚拟机 `D:\VMs\DesOS\DesOS.vmx`：`other-64` / 512 MiB / 1 vCPU / 无网卡 / 无硬盘 / 默认 BIOS / 硬件版本 21；`ide1:0` cdrom-image 指向 `E:\Mine\A_Desael3\DesOS\build\DesOS.iso`；`serial0` 输出到 `D:\VMs\DesOS\serial.log`。完整串口日志存 `build/vmware_p2_serial.log`（5078 字节）。
- 判定链全过：`Limine 12.9.6 (x86-64, BIOS)` → `limine: Loading executable boot():/kernel.elf` / `Base revision: 1` / `Requests count: 4` → `DesOS boot OK` → B.1/B.2/B.3/B.4/B.5/B.6/B.6b/B.7/B.8/B.9/B.10/B.11 全部 `selftest: PASS` → `Hello from DesOS userland (ring3)!`（自检与 `run` 各一次）→ `B-STAGE: PASS (failed=0)` → `[B.11] interactive shell: COM1 serial + PS/2 keyboard (IRQ1)`。
- 窗口为深蓝底 + `DesOS boot OK` 点阵字（VBE mode `0x141`，1024x768x32，framebuffer `0xe8000000`），与 QEMU 一致；自检与 Shell 只走 COM1，符合 `kprintf()` 设计。
- **跨 VMM 差异（本次新增证据）**：VMware MADT 为 `ver=0x15 overrides=1 gsi0=2`（QEMU `ver=0x14 overrides=5`），`B.6b` 仍在 APIC 模式下真实投递 3 次 IRQ0 并 PASS；`[B.1] usable=130634 pages (510 MiB)`（QEMU 130679）；`[B.7] ticks=3 switches=5`（QEMU 15/17）；另出现 QEMU 没有的 `acpi: "DMAR" not found` / `"IVRS" not found`（IOMMU 表缺失，不影响引导）。说明 APIC / PMM / 调度链路对第二种 VMM 的拓扑差异是稳的。
- **P.2 踩坑**（串口输出文件必须事先存在 / 开机选「替换」、移除硬盘必须点「确定」、`startConnected` 默认 TRUE）见 `NOTES.md` 第 5 节。
- **BIOS 路径未单独验**：真实按键（日志里的 `DesOS> help/echo/mem/bogus/run/ticks` 是 `shell_inject()` 自检注入，不是手工敲的）；已在 UEFI 模式下补验，见下。

### UEFI 补充验证（2026-11-07，VMware UEFI）

- 同一虚拟机改 `firmware = "efi"`，未启用 Secure Boot。`vmware.log` 佐证：`The EFI ROM is EFI2.7 64-bit.`、`About to do EFI boot: EFI VMware Virtual IDE CDROM Drive (IDE 1:0)`、`Firmware has transitioned to runtime.`。串口日志存 `build/vmware_p2_uefi_serial.log`（3337 字节，含后续真实按键）。
- **引导成功**：UEFI 从 El Torito EFI 项加载 `limine-uefi-cd.bin` → `BOOTX64.EFI` → `kernel.elf`；窗口同样绘出深蓝底 `DesOS boot OK`；内核 `kprintf()` 照常写 COM1，B.1/B.2/B.3/B.4/B.5/B.6/B.6b/B.8/B.9/B.10/B.11 全 PASS，ring3 输出两次，`[B.11] interactive shell` 出现。
- **Limine 引导日志在 UEFI 下不写 COM1**：日志直接从 `DesOS boot OK` 起，没有 BIOS 路径里的 `vbe:` / `Limine 12.9.6 (x86-64, BIOS)` / 菜单倒计时 / `Loading executable`。即 Limine UEFI 走 GOP 控制台，内核自身串口驱动不受影响。
- **发现一个跨固件差异（根因与修复见 `NOTES.md` 第 4 节）**：`[B.7] selftest: FAIL (ticks=1 switches=3, time-slice preemption)` → `B-STAGE: FAIL (failed=1)`。B.7 阶段只攒到 1 个 PIT tick、3 次上下文切换，未达到自检要求的 4 次；worker A/B 各自连续跑完 3 步、没按时间片交替。B.6 `irq0=3 ticks=3` 与 B.6b `irq0+=3 mode=APIC` 仍 PASS，B.8–B.11 全 PASS。**不影响引导与 Shell，不是 B 阶段回归**。该 FAIL 已在「B.7 修复记录」中消除，本段保留为当时的原始观测。
- 内存布局差异：`usable=123363 pages (481 MiB)`（BIOS 130634 / 510 MiB），UEFI 固件多占约 29 MiB；`bitmap@0x2000`（BIOS `0x7e000`）；MADT 仍为 `ver=0x15 overrides=1`。

### 真实按键补充验证（2026-11-07，VMware UEFI，任务 3）

- 在 UEFI 模式、Shell 交互循环里按 `Ctrl + G` 定向键盘，手工敲 `version` 回车。串口日志从 3268 增至 3337 字节，新增 `version` → `DesOS 0.1 x86_64 - stage B complete (M1/M2 reached)` → 新的 `DesOS>` 提示符。
- 结论：**VMware 的 PS/2 IRQ1 键盘输入链路真实可用**，「输入 → 回显 → 解析执行 → 输出 → 新提示符」全通；原先把「VMware 默认走 USB HID、敲不了字」列为已知差异的担心并未发生。这也补上了 B.11 验收只能用 `shell_inject()` 注入命令的缺口。
- 未单独验：BIOS 模式下的真实按键、鼠标 / 修饰键 / 扩展键（属 C.1）。

---

## 阶段 C：设备与驱动（2026-11-07，仅 QEMU 验收）

### C.1a 验收记录（2026-11-07，仅 QEMU）

- 改动文件：`kernel/inc/kbd.h`、`kernel/src/kbd.c`、`kernel/inc/shell.h`、`kernel/src/shell.c`、`kernel/src/b_selftest.c`。B.11 的 `IRQ1 -> kbd_inject_scancode() -> shell_input_push()` 链路保持不变。
- 实现：PS/2 set 1 状态机跟踪左右 Shift/Ctrl/Alt（靠 `0xE0` 区分右 Ctrl / 右 Alt）、Caps/Num/Scroll 锁定键；`shift ^ caps` 决定字母大小写，Shift 上档表给符号，Ctrl/Alt 组合不向 Shell 插字符（Ctrl+字母给控制字符）；`0xE0` 扩展键只记 `0xE0xx` 键码、不提交字符，行编辑留 C.1b。
- 自检：`b_selftest.c` 的 `test_keyboard()` 用 `kbd_inject_scancode()` 注入 8 组序列（小写回归 / 左 Shift / 上档符号 + 右 Shift / CapsLock 与 Caps+Shift / Ctrl+C / E0 右 Ctrl / 上箭头 / 主键盘 Enter），走与 IRQ1 完全相同的链路；末尾用 `shell_input_flush()` 清掉残留输入，避免交互 Shell 执行自检字符。
- 构建：`wsl bash -c "bash /mnt/e/Mine/A_Desael3/DesOS/tools/make_iso.sh"`，无编译警告。
- 验收（**按 `promat.md` 第 14 条，只用 Windows QEMU，未开 VMware**）：`powershell -ExecutionPolicy Bypass -File tools\run_qemu_btest.ps1` → 16 项回归全 PASS、`B-STAGE: PASS (failed=0)`、`[C.1] selftest: PASS (mods=0x0 events=10 key=0x1c last=?, PS/2 modifiers+E0)`、`C-STAGE: PASS (failed=0)`，随后正常进入 `[B.11] interactive shell`。日志 `build/c1a_qemu_serial.log`（5629 字节副本）、`build/b_selftest_qemu.log`。
- 日志里 `last=?` 表示最后一次注入的是不可打印的回车控制字符；`events=10` 与 8 组序列提交的字符数一致，扩展键未提交字符。

### C.1b 验收记录（2026-11-07，仅 QEMU）

- 改动：`shell.h` 定义 `KEY_UP/DOWN/LEFT/RIGHT/HOME/END/DELETE/INSERT/PGUP/PGDN`（0x80–0x89）；`kbd.c` 用 `ext_key_map[128]` 把 `0xE0` 扩展键翻译成控制码送进 Shell，新增 `kbd_control_events()`；`shell.c` 行缓冲加 `cursor` / `rendered_len` 与 `redraw_line()`（串口无 ANSI 光标控制，用 `\r` 重打整行 + 空格擦尾 + `\b` 回光标），支持左右移动 / Home / End / Delete / 中间插入与删除；`b_selftest.c` 的 `test_keyboard_editing()` 注入 `abc → Left×2 → 插入 X → Home → Delete → End → 退格 → 回车`，走完整链路校验行内容与光标位置。
- Up/Down/PgUp/PgDn/Insert 只识别不改行（暂无命令历史 / 覆盖模式）。
- 验收：16 项回归全 PASS，`[C.1] line-edit selftest: PASS (keys=5 line=0 cursor=0, arrows/Home/End/Delete)`、`C-STAGE: PASS (failed=0)`。
- 纯文本串口日志不解释 `\r`，重绘看起来像 `DesOS> hDesOS> he...` 的重复前缀；真实终端里光标回到行首，显示正常。

### C.1c 验收记录（2026-11-07，仅 QEMU）

- 新增 `kernel/inc/mouse.h`、`kernel/src/mouse.c`：8042 配置（`0xA8` 启用第二端口、`0x20`/`0x60` 配置字节置 IRQ12 使能并清 Mouse Clock Disable、`0xF6` Set Defaults、`0xF4` Enable Data Reporting）、IRQ12 标准 3 字节包解析（`(b & 0xC8) == 0x08` 对齐并挡掉 ACK `0xFA`、9 位补码 X/Y、溢出位忽略）、状态与计数接口。
- `apic.c` 在 IOAPIC 初始化时把 `IRQ12 -> 向量 44` 一起放行；`idt.c` 把 IRQ12 分发到 `mouse_on_irq12()`；`kernel_main.c` 在 `b_selftest_run()` 之后、`shell_run()` 之前调 `mouse_init()`（必须晚于 `apic_init()`，否则 IOAPIC 重配会覆盖路由）；`shell.c` 加 `mouse` 命令与 help 列表。
- `b_selftest.c` 的 `test_mouse()` 注入 4 个包（正位移 / 左键 / 负位移 / ACK 错位恢复），并读回 IOAPIC 重定向表确认 `gsi12=12 vec=44` 未屏蔽。
- 验收：16 项回归全 PASS、`B.6b APIC` 仍 PASS、`[C.1] mouse selftest: PASS (packets=4 x=14 y=3 btn=0 gsi12=12 vec=44)`、`[C.1] PS/2 mouse enabled (IRQ12 -> vector 44, cfg=0x43)`、`B-STAGE: PASS (failed=0)`、`C-STAGE: PASS (failed=0)`，随后正常进入交互 Shell，无 IRQ12 中断风暴。日志 `build/c1_qemu_serial.log`（6552 字节副本）。
- **C.1 三步全部完成，但阶段 C 整体未完成**（C.2 / C.3 / C.4–C.7 待做）。按 `promat.md` 第 14 条仍**未启动 VMware**；真实鼠标移动留在阶段 C 整体完成后的图形窗口 / VMware 冒烟测试。

### C.2 验收记录（2026-11-07，仅 QEMU）

- 新增 `kernel/inc/fb.h`、`kernel/src/fb.c`：M1 的临时点阵绘制迁到驱动模块，补上完整 5x7 可打印 ASCII 字库（32–126）、`fb_pset` / `fb_pget` / `fb_fill_rect` / `fb_clear` / `fb_draw_char` / `fb_draw_string`（全部边界裁剪，`fb_rgb` 按帧缓冲掩码拼色），以及 `fb_console_*` 文本控制台（`\n` / 退格 / 自动换行 / 满屏用 `kmemmove` 滚动）。
- `kprintf.c` 的 `kputc` 增加 `if (fb_ready()) fb_console_putc(c)`，同一份日志同时进 COM1 与屏幕；`fb_ready()` 保证 `fb_init()` 之前的早期输出不会踩空指针。`kernel_main.c` 删掉临时字体/绘制函数，改为 `fb_init` → `fb_clear` → 画 M1 大字 → `fb_console_init(40, 150, ...)`，日志显示在大字下方，M1 画面保持不变。
- 自检 `test_framebuffer()` 真写显存再读回：相邻像素互不影响、矩形四角/内部填充色、字符串绘制区域内出现前景色、越界写入不改写界内像素、控制台累计行数 > 0。
- 验收（**按 `promat.md` 第 14 条，只用 Windows QEMU**）：16 项回归全 PASS、`B-STAGE: PASS (failed=0)`、`[C.2] selftest: PASS (1024x768 bpp=32 rows=71, framebuffer)`、`C-STAGE: PASS (failed=0)`，正常进入交互 Shell。日志 `build/c2_qemu_serial.log`（6614 字节副本）。
- **副产物印证 B.7 修复**：控制台绘制让 B.7 的 `ticks` 从 10 涨到 19、`switches=21`，按 tick 等待的判据仍 PASS；若还是旧的固定循环忙等，这里极可能再次 flaky。
- **C.2 完成，C.3 也已验收**；阶段 C 整体仍未完成（C.4 磁盘 / C.5 网络 / C.6 USB / C.7 ACPI 待做），仍未启动 VMware。帧缓冲画面靠显存读回验证、PCI 枚举靠 class code 比对验证；可见窗口 / 截图验证留到阶段 C 整体完成后的冒烟测试。

### C.3 验收记录（2026-11-07，仅 QEMU）

- 新增 `kernel/inc/pci.h`、`kernel/src/pci.c`：经典 PCI 配置机制 #1（`outl(0xCF8, 0x80000000|bus<<16|dev<<11|fn<<8|reg)`、`inl(0xCFC)`），`pci_read/write32/16/8`、`pci_init()`（写 0x80000000 回读 enable 位探测机制）、`pci_enumerate()`、`pci_find_class()`。当前只枚举 bus 0–7（QEMU/SeaBIOS 设备都在 bus 0），多根总线留待后续。
- 自检 `test_pci()` 真扫 BDF：至少 1 个有效设备（vendor != 0xFFFF）、能读到 class code（基类非 0）；打印前 8 个设备（BDF / vendor:device / class），并用 `pci_find_class(0x03,0x00)` 确认 VGA 与 C.2 帧缓冲同源。
- **修了一个真实 bug**：PCI 配置空间 0x08 是 `0x08=Revision / 0x09=ProgIF / 0x0A=Sub-Class / 0x0B=Base Class`（小端），初版把 Base/Sub/ProgIF 错当成 `(cls>>16)/(cls>>8)/(cls&0xFF)`，整条右移 8 位，导致 VGA 报 `class 0000/02`、`vga: no`。改成 `>>24/>>16/>>8` 后正确：Host 0600、ISA 0601、IDE 0101、ACPI 0680、VGA 0300、e1000 0200，`vga: yes`。
- 验收（**按 `promat.md` 第 14 条，只用 Windows QEMU**）：16 项回归全 PASS、`[C.3] scan: 6 device(s)`、`[C.3] selftest: PASS (devices=6 class_ok=1 vga=yes)`、`C-STAGE: PASS (failed=0)`，正常进入交互 Shell。日志 `build/c3_qemu_serial.log`（6954 字节副本）。
- **工具坑**：`tools/run_qemu_btest.ps1` 含中文，必须用 `-File` 跑；用 `-Command "& '...'"` 会因中文编码触发 PowerShell 解析错误（missing terminator）而整脚本不执行，日志也不会更新。

### 阶段 C 收官：VMware 双固件冒烟 + C.4–C.7 记录说明（2026-11-08）

- **C.4 磁盘 / C.5 网络 / C.6 USB / C.7 ACPI 的验收记录与踩坑详见 `ProjectRoad.md` 阶段 C 各条目**（本文件阶段 C 段落止于 C.3；C.4 起的记录写进了路线图对应小节）。QEMU 自动验收脚本 `run_qemu_btest.ps1` 已扩到 B.1–B.11 + C.1–C.7，全部 PASS。
- **VMware 双固件冒烟（阶段 C 整体完成后按 `promat.md` 第 14 条执行，UEFI 轮 + BIOS 轮都做）**：同一台 `D:\VMs\DesOS\DesOS.vmx`，串口输出到 `build/vmware_c_serial.log`。两轮均为 `B-STAGE: PASS (failed=0)` + `C-STAGE: PASS (failed=0)`；真实 PS/2 键盘逐字符输入、行编辑、鼠标真实移动（`x=38 y=47 packets=7 irq12=23`，BIOS 轮 `x=157 y=159 packets=10`）全部正常；C.4 磁盘 / C.5 网络 / C.6 USB 因 VMware 无 IDE 盘 / e1000 网卡 / xHCI 控制器优雅跳过（PASS-skipped，符合预期）。
- **固件判定**：第一轮日志直接以 `DesOS boot OK` 开头（Limine UEFI 不写 COM1）、`bitmap@0x2000` —— 实为 **UEFI 轮**（11/07 B.7 修复时已把固件改成 efi，当时误记为 BIOS）；第二轮日志含 Limine BIOS 菜单 + `vbe:` 行、`bitmap@0x7e000` —— 为 **BIOS 轮**。
- **C.7 跨 VMM / 跨固件证据（三套 ACPI 环境全通过）**：UEFI `oem=VMWARE pm1a=0x444`、BIOS `oem=PTLTD pm1a=0x1004`、QEMU `pm1a=0x604` —— 电源管理端口全部从各自 FADT 动态解析而非写死；`\_S5 typ=0` 均从真实 DSDT 扫出。
- **`reboot` 真机验证 PASS（两轮）**：Shell 敲 `reboot` 后 VM 整机重启，串口日志出现第二轮完整 boot（UEFI 轮、BIOS 轮均验证）。
- **`poweroff` 真机验证 PASS（两轮）**：Shell 敲 `poweroff` → `poweroff: ACPI S5 request...`（UEFI 写 0x444 / BIOS 写 0x1004）→ VM 真断电关机，日志终止于该行。
- **发现并修复一个自检竞态（flaky test，非内核缺陷）**：第一轮第二次 boot 的 `[B.11] selftest: FAIL (commands=4 lines=5)`。根因：`test_shell()` 注入期间键盘 IRQ1 未屏蔽，真实键盘事件与 `kbd_inject_scancode(0x1C)`（模拟 Enter）竞争 kbd 状态机，Enter 被吃掉 → 最后一行未提交。修复：新增 `apic_mask_gsi()`（IOAPIC redir bit16 屏蔽/放开单个引脚），`test_shell()` 注入前屏蔽 IRQ1、结束恢复（PIC 回退路径用 `pic_set/clear_mask(1)`）。修复后 QEMU 全量回归 `RESULT: PASS`；第二轮（BIOS）双 boot 的 B.11 均 PASS，交叉印证。

---

## 阶段 D：用户态 / 权限 / 服务 / CLI（2026-11-08）

> 每一步的实现细节、目的与已知限制见 `ProjectRoad.md` 阶段 D 各条目；本段只记验收证据与日志落点。

- **D.3a 服务注册表**：`SYS_SVC_INFO` 内核单源，Shell `services` 与用户态 `svc_count()` / `svc_info()` 共用。验收抓到并修复 `svc_count()` 返回 -1 的真 bug（`idx<0` 计数查询被用户缓冲校验提前拦截），修复后用户态读到 `services: 7`（`testd` 在 hello 之后注册，故为 7 而非 8）+ 逐行状态正确。
- **D.3b 多程序 + 真 init**：`user/apps/*.c` 各自链接成 ELF，`NASM incbin` 内嵌并生成 manifest `userapp_table[]`；`init_launch()` 以 `PRIV_SYSTEM` 拉起 init 用户进程。串口实测 `[D.3] launching init (priv=system, pid=7)` → `init: DesOS userspace started (pid=7 priv=1)` → 8 条服务逐行汇报（4 running kernel / 3 stopped system no hardware / testd running system）→ `init: done` → `[D.3] init: exited`。修复 `launching init` 与用户 printf 串口字符交错（打印期间 `cli`）。
- **D.4 CLI 单一数据源**：`tools/gen_cli_docs.py` 的 `COMMANDS` 表（16 命令 / 7 别名 / 5 分组）生成 `kernel/inc/cli_commands.h`、`docs/cli-help.txt`、`docs/cli-commands.md`；`build_kernel.py` 每次构建前自动重生成，失败即中止。串口实测 `DesOS CLI - command list`、`usage: run [app] [priv]`、`usage: ticks`、`whoami: main`、别名 `stage D (libc / privilege / init / CLI)`。
- **D.5 静默启动与交互修复（用户反馈）**：屏幕开机只显示干净欢迎画面 + `DesOS>`，自检 / 命令演示 / init 汇报全部只写 COM1（日志完整）；`verbose [on|off]` 随时切换屏幕镜像。修复「必须按任意键才显示提示符」与「吞掉第一个字符」（脏 `rendered_len` + 首帧 prompt 缺失 + `kbd_init()` 残留字节），新增 `shell_prepare_interactive()` 与 `test_shell_startup()` 防回归。
- **VMware 用户复验日志 `build/vmware_c_serial.log`（15511 字节，11/08 15:39）**：`[D.5] console: quiet boot` → `[D.5] shell startup selftest: PASS (first key preserved)` → `B-STAGE: PASS (failed=0)` / `C-STAGE: PASS (failed=0)` → `[B.11] interactive shell` → 手工逐字符输入 `run` / `run hello` / `poweroff` 首字母均完整 → `poweroff: ACPI S5 request...` 后日志终止（真关机）。该日志同时是 D.5 修复在真实 PS/2 键盘路径上的验收证据。
- **QEMU 全量自动验收 `build/b_selftest_qemu.log`**：`tools/run_qemu_btest.ps1` 的 43 项串口检查 + 3 份 CLI 产物存在性检查全绿，`RESULT: PASS - B.1-B.11 + C.1-C.7 + D.1-D.5 all self-tests passed`。**工作区整理后 11/08 16:04 重建复验**：把 `test_shell_startup()` 中的伪 banner 去掉（改为只 `shell_input_flush()`），串口日志里 `[B.11] interactive shell` 从两次收敛为一次（仅由真正的 `shell_run()` 打印），43 项仍全 PASS，首帧 prompt 检查照常命中。
- **工作区日志清理（2026-11-08）**：`build/` 下全部 `*.log` 已清空，含当前验收日志、9 个历史串口证据日志，以及 `keytest.log` / `poweroff_test.log` / `poweroff_wsl.log` 三个 C.7 调试期副本（后三者无文档引用且无独有证据：前两个尾部只到 `[B.11] interactive shell`，约 50 处 `DesOS> ` 均为自检注入的命令演示，无真实按键 / 无 `ACPI S5`；第三个的真关机证据已由 `vmware_c_serial.log` 当时以真实 PS/2 按键路径更完整覆盖）。C.6 xHCI 五轮迭代的 QEMU trace / 串口中间产物、全部可重建的编译中间产物（`*.o` / `iso_root/` / `user/` / `user_image.asm`）也已清理，`make_iso.sh` 全量重建即可恢复。各阶段的判定结果、串口片段与复现命令仍完整保留在本文正文中，删掉本地副本不影响证据链；清单见 `README.md`「构建产物与日志」。
