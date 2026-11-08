# DesOS 注意事项与踩坑记录

> 本文只记「踩了什么坑、为什么、怎么解决」。进度与每一步的目的见 `ProjectRoad.md`，构建命令见 `README.md`，工作习惯见 `promat.md`。
> 记录时间：2026-11-07。所有结论都来自真实跑过的命令 / 串口日志，不靠推断。

---

## 1. Limine 配置与 ISO 构建

### 1.1 配置语法（最主要，A 阶段）
- **现象**：停在菜单（默认 5 秒倒计时）、报 `Default entry is not valid`，根本不加载内核。
- **原因**：Limine 12.x 语法改了。用 `=` 分隔、菜单项写成 `:DesOS`、行内注释、CRLF 行尾都会让选项 / 条目解析不到。串口出现 `Booting automatically in 5...` 就是配置没解析。
- **解决**：改成 `option: value`（冒号）、菜单项以 `/` 开头、注释单独成行、全局项放在首个菜单项之前、`build_iso.py` 把 `limine.conf` 统一转 LF。
- **注意**：选项名本身**不区分大小写**（CONFIG.md 明确），改大小写不解决问题。

### 1.2 `.limine_reqs` 段名
- **现象**：`PANIC: limine: .limine_reqs entry outside kernel image`，拒绝引导。
- **原因**：base revision 0 下 Limine 会按已废弃机制把该名字的段解释为「请求指针数组」。
- **解决**：段改名 `.limine_reqs_data` + 升到 **base revision 1**（tag 放在 start/end 分隔符之间）。

### 1.3 `.bss` 用 `NOLOAD`
- **现象**：`call kernel_main` 一压栈就 #PF → 三重错误。
- **原因**：Limine 不映射 `NOLOAD` 段。
- **解决**：`.bss` 并入可写 LOAD 段。

### 1.4 LMA = 更高半 VMA
- **现象**：物理载入地址非法，加载失败。
- **解决**：链接脚本用 `.text : AT(0x100000)` 让 LMA 落到低位物理地址，VMA 保持 `0xFFFFFFFF80000000`。C 必须带 `-mcmodel=kernel -mno-red-zone`。

### 1.5 ISO 启动镜像被截断
- **现象**：SeaBIOS 加载不了 Limine。
- **原因**：`-boot-load-size 4` 只拷 2048 字节。
- **解决**：按 `limine-bios-cd.bin` 实际大小设 56 扇区。

### 1.6 `./configure` 默认关 BIOS
- **原因**：Limine 带 `LIMINE_NO_BIOS` 默认值，而 A.7 用 `qemu -cdrom` 需要 BIOS 路径。
- **解决**：显式加 `--enable-bios --enable-bios-cd --enable-uefi-x86-64 --enable-uefi-cd`。

### 1.7 工作目录清理后的重编
- `tools/limine/limine-12.9.6/` 只保留构建必需文件（`bin/` 的 `limine` / `limine-bios.sys` / `limine-bios-cd.bin` / `limine-uefi-cd.bin` / `BOOTX64.EFI`，以及 `limine-protocol/include/limine.h`）。完整源码在 `tools/limine/limine-12.9.6.tar.xz`，要重编先解压再跑 `tools/limine/build_limine.sh`。

### 1.8 排障方法（通用）
- `serial: yes` + QEMU `-nographic` 可把 Limine 引导日志 / panic 打到串口。
- `-display none` + monitor `screendump` 抓帧缓冲转 PNG，可「读」出只显示在 VGA 上的错误。

---

## 2. 汇编、GDT / IDT（B 阶段第二批）

1. **NASM `mov ds, ax` 实际编码为 `mov ds, eax`（32 位形式）**，高 16 位是垃圾 → 加载垃圾选择子 #GP。必须先 `mov eax, edx` 清高 16 位。
2. **长模式重载 CS 必须 `retfq`（`48 CB`）**；`retf`（`CB`）是 32 位远返回，只弹 32 位 RIP/CS。
3. **NASM 的 `ltr` 只接受 16 位寄存器写法**（`ltr di`）。
4. **同名 asm / C 源文件产物互相覆盖**（`gdt.asm` 与 `gdt.c`）→ 汇编产物统一加 `asm_` 前缀，`build_kernel.py` 改为汇编 `kernel/asm/*.asm` 全部文件。
5. **`serial_init()` 在 QEMU 下会卡在 LSR 等待**：SeaBIOS 已设好 115200 8N1，所以 `kernel_main` 直接发字节，不跑初始化等待。

---

## 3. 用户态与 APIC（B 阶段第四批）

1. **用户栈 4 页从 `0x7FFFFFFFD000` 起**：第 4 页落进 `0x0000800000000000`（低半 canonical 上界之外 / PML4[256] 的 HHDM），`vmm_map_user` 返回 -4。整体下移一页到 `0x7FFFFFFFC000` 解决。
2. **APIC 模式下 IRQ 结束必须写 LAPIC EOI**，`pic_send_eoi()` 无效 → `idt.c` 改成按 `apic_enabled()` 二选一发 EOI。
3. **QEMU 的 MADT 有 `IRQ0 → GSI 2` override**：只配 IOAPIC pin 0 收不到 PIT，必须同时配 pin 0 与 override 的 GSI。
4. **伪中断向量**用 `0x2F` 复用已安装的 IRQ15 桩，避免跳到空 IDT 项。
5. **`apic_fallback_to_pic()`**：APIC 验证失败时退回 B.6a 的 PIC 路径，保证后续自检不挂死。
6. **B.6b 的 PASS 是真的**：自检读回 IOAPIC 重定向表（PIT→32、键盘→33、未屏蔽）、确认 `IMR=0xff/0xff`，再 `sti` 让 PIT 经 IOAPIC 连续投递 3 次 IRQ0；LAPIC EOI 写错则第 2/3 次不会到达。
7. **B.11 自动验收无法注入真实按键**：自检用 `shell_inject()` 走「输入→回显→解析执行」，键盘路径用 `kbd_inject_scancode(0x1C)`（Enter）；`-nographic` 下敲不了字，真实按键要在有窗口 / 串口输入的 QEMU 或 VMware 里验。

---

## 4. 自检脚本：B.7 flaky test（重点）

### 现象
VMware 17.6.4 + UEFI 下 `[B.7] selftest: FAIL (ticks=1 switches=3, time-slice preemption)` → `B-STAGE: FAIL (failed=1)`；B.6 / B.6b / B.8–B.11 全 PASS，引导与 Shell 正常。

### 根因
- 判据：`b_selftest.c` 的 `ok7 = (timer_ticks() > 0) && (sched_switch_count() >= switches0 + 4)`；`timer.c` 的 `TICKS_PER_SLICE = 1` 让每个 PIT tick 都请求一次重调度。
- worker 用 `busy_wait(2500000)`（**固定循环次数，不是固定时间**）等时间片，隐含认定「这段忙等会跨过足够多的 10ms tick」。
- VMware 走硬件 VT-x、VCPU 接近原生速度，6 段忙等在 ≤1 个 tick 内跑完，只发生 3 次切换（main→A、A→B、B→main），差 1 次到硬阈值；QEMU 软件模拟慢，同样代码攒到 14–15 个 tick。

### 交叉验证（同一 ISO，一字节未改）
| 环境 | B.7 ticks | switches | 结果 |
|------|-----------|----------|------|
| QEMU 11.1.0 + SeaBIOS | 15 | 17 | PASS |
| QEMU 11.1.0 + OVMF `edk2-x86_64-code.fd` | 14 | 16 | PASS |
| VMware 17.6.4 + BIOS | 3 | 5 | PASS |
| VMware 17.6.4 + UEFI | 1 | 3 | FAIL |

- QEMU+OVMF 日志 `build/qemu_uefi_serial.log` 含 `BdsDxe: starting Boot0001 "UEFI QEMU DVD-ROM..."` 与 `B-STAGE: PASS (failed=0)`，证明 UEFI 路径本身没问题。
- 用 `vmrun -T ws stop/start ... nogui` 无界面复跑，仍是逐字一致的 `ticks=1 switches=3`，不是偶发抖动。
- **结论：测试脆弱性（flaky test），不是内核缺陷。**

### 解决（已实施）
- 删掉 `busy_wait(2500000)`，改为 `wait_for_ticks(B7_WAIT_TICKS)`（`= 2`）：worker 每步等 PIT tick 至少推进 2 次，等待时长由 100 Hz 定时器决定，与 CPU / VMM 速度解耦；循环带 `B7_SPIN_GUARD`，PIT 失效时只会判 FAIL，不会挂死。
- 新增 `step_owner[8]` / `log_step()` / `steps_alternated()`：`ok7` 额外要求步序中出现 A/B 相邻交替，PASS 才真正证明时间片抢占；日志加 `alt=` 字段。
- 修复后 QEMU（SeaBIOS）与 VMware（UEFI）都得到 `ticks=10 switches=12 alt=1`，A/B 步序一致 `A1 B1 A2 B2 A3 B3`，`B-STAGE: PASS (failed=0)`。证据：`build/b_selftest_qemu.log`、`build/vmware_b7_fixed_uefi_serial.log`。

---

## 5. VMware / 固件 / 下载

1. **串口输出文件必须事先存在**，否则设置路径时弹「路径不存在」；开机又问「附加 / 替换 / 取消」，选**替换**保证日志只含本次启动。开机前删掉 `serial.log` 可绕开弹窗，VMware 会新建。
2. **移除硬盘必须点「确定」才落盘**：VMX 残留 `ide0:0.fileName` 但无 `ide0:0.present = "TRUE"` 才是真的停用；只有 `present` 时硬盘仍挂载。
3. VMX 省略 `startConnected`（默认 TRUE），开机弹串口文件提示即连接生效的实证。
4. **VMware 静默安装**：`/s /v"/qn EULAS_AGREED=1 INSTALLDIR=D:\VMware\Workstation AUTOSOFTWAREUPDATE=0"`。额外加 MSI 日志开关 `/l*v` 会让 bootstrapper 把 `EULAS_AGREED` 截成 `ULAS_AGREED`，`VM_ValidateEULAAccepted` 返回 3 → MSI 1603；去掉 `/l*v` 后成功。
5. **下载渠道**：`downloads.broadcom.com` 302 跳 OAuth 登录（与「不登录不注册」约束冲突）、`download3.vmware.com` 已下线、官方 CDS 只剩 `info-only`、winget 无包。最终走第三方 GitHub Releases 镜像，全程无需账号；**必须校验** SHA-256 与 Authenticode 签名确认是 Broadcom 官方原版。镜像仓库内的 KeyGen 未下载、未使用（17.6.4 免费无需激活）。装完删安装包。
6. **Secure Boot 不要启用**：Limine `BOOTX64.EFI` 未签名。
7. **UEFI 下 Limine 引导日志不写 COM1**：走 GOP 控制台，串口日志直接从 `DesOS boot OK` 起，没有 `vbe:` / 菜单倒计时 / `Loading executable`；内核 `kprintf()` 仍写 COM1，不受影响。
8. **跨固件 / 跨 VMM 差异（非故障）**：UEFI 下 `usable=123363 pages (481 MiB)`（BIOS 130634 / 510 MiB，固件多占约 29 MiB）、`bitmap@0x2000`（BIOS `0x7e000`）；VMware MADT `ver=0x15 overrides=1`（QEMU `ver=0x14 overrides=5`）；VMware 多出 `acpi: "DMAR" not found` / `"IVRS" not found`（IOMMU 表缺失，不影响引导）。
9. **原担心 VMware 默认走 USB HID 敲不了字**：实测 UEFI 下按 `Ctrl + G` 定向键盘后手工敲 `version` 回车，串口日志新增 `version` → `DesOS 0.1 x86_64 - stage B complete (M1/M2 reached)` → 新提示符。PS/2 IRQ1 真实按键链路可用。
10. `vmrun stop` 在 VM 已关机时报 `The virtual machine is not powered on`，非致命，忽略即可继续 `start`。
11. QEMU UEFI `-nographic` 输出混入大量 ANSI 转义，会让 `Select-String` 结果看起来被截断；先用正则 `\x1b\[[0-9;?]*[A-Za-z]` 清掉再提取关键行。

---

## 6. 命令与工具链

1. **PowerShell 把 `;` 当分隔符**：多条命令走 WSL 时必须整体包进 `wsl bash -c '...'`（单引号），否则后半段在 PowerShell 里跑（报 `wc` 不是 cmdlet 之类）。
2. **WSL 里 `export PATH=...` 含 `(x86)` 要加双引号**：`export PATH="$HOME/opt/cross/bin:$PATH"`，否则 bash 把 `(` 当数组语法报错。
3. **`Get-Content` 读日志被拦**（未显式指定编码）→ 用 `Get-Content ... -Encoding UTF8`。
4. 交叉链解析不到 PATH 时，`build_kernel.py` 会回退 `~/opt/cross/bin`。
5. `password.for.wsl.lock` 存 sudo 密码，需要提权时用 `sudo -S` 喂入；该文件已被 `.gitignore` 排除，绝不入库。

---

## 7. 已知限制 / 待办

- 阶段 0 仅剩 `0.27 sudo -S make install`（`limine --version` 验证），不阻塞构建。
- **阶段 A / B / C / D 主线已全部完成，阶段 E（兼容层）待开始**。C.1–C.7、D.1–D.5 的验收记录见 `log.md`，进度与每步目的见 `ProjectRoad.md`。
- **D.2b VFS ACL 顺延**：依赖后续文件系统阶段，尚未实现；`init` 目前是「监督 / 汇报」骨架，服务动态 start/stop 需等 IPC + `SYS_SVC_CTRL`；`elf_load()` 每次加载的用户页 / PML4 尚未回收（阶段 E 内存回收一并处理）；`SYS_SVC_INFO` 的用户指针只做低半 canonical 范围校验（逐页 uaccess 待 VMM 审查）。
- 帧缓冲只支持 32bpp、ASCII 位图字体（C 命令表与 `docs/cli-help.txt` 走英文，避免屏幕乱码）；画面靠显存读回自检验证，未做图形窗口截图。Up/Down/PgUp/PgDn/Insert 只识别不改行（无命令历史）。C.4 磁盘 / C.5 网络 / C.6 USB 在无对应硬件的 VMware 环境会优雅跳过。
- **验收分级**（`promat.md` 第 14 条）：小步骤只在 Windows 版 QEMU 上验，整个大阶段完成后才用 VMware 做 BIOS / UEFI 跨 VMM 冒烟。阶段 C / D 完成后均已上 VMware 复验（含真实 PS/2 键盘逐字符输入、行编辑、鼠标真实移动、`poweroff` ACPI S5 真关机与 `reboot` 整机重启）。
- `build/` 下**所有串口日志副本已清理（2026-11-08）**，只保留 `kernel.elf` / `DesOS.iso` / `disk.img` 三个最终产物；判定结果、串口片段与复现命令已抄录在 `log.md`，需要重新取证跑一次 `run_qemu_btest.ps1` 即可重新生成当前验收日志。本文件其它位置出现的 `build/*.log` 路径是当时的记录标识，不代表文件仍存在。C.6 xHCI 调试 trace 与全部编译中间产物也已删除，跑 `make_iso.sh` 全量重建即可恢复；清单见 `README.md`「构建产物与日志」。
- VMware 虚拟机目前关机，VMX 仍为 `firmware = "efi"`；要恢复可见窗口或切回 BIOS 需另行操作。

---

## 8. 阶段 C 实现要点

- **自检注入会污染 Shell 输入缓冲**：`test_keyboard()` 通过 `kbd_inject_scancode()` 注入的按键会真的进 `shell_input_push()` 的环形缓冲。若自检结束不清空，`shell_run()` 一启动就会把残留字符当成用户命令回显 / 执行。因此 `shell.c` 提供 `shell_input_flush()`，自检末尾必须调用；`kbd_reset()` 同时清空修饰键状态。
- **`0xE0` 伪 Shift**：`0xE0 0x2A` / `0xE0 0x36` 是 Print Screen 序列里的伪 Shift，不能按左 / 右 Shift 处理，否则会残留一个永远按着的 Shift 状态。
- **锁定键只在按下时翻转**：Caps / Num / Scroll 的断码（松开）不能再次 toggle，否则按一次等于没按。
- **Ctrl / Alt 组合不插字符**：避免 Alt+字母把字母混进行缓冲；Ctrl+字母统一给 `0x01..0x1A` 控制字符。
- **大小写规则是 `shift XOR caps`**，且只作用于字母；数字 / 符号的上档只看 Shift 表，Caps 不影响 `1` 这类键。
- **C.1a 只识别扩展键、不做行编辑**：`0xE0xx` 键码记在 `kbd_last_key()`，方向键 / Home / End / Delete 的编辑行为留 C.1b，避免一步改太多。
- **C.1b 的串口重绘**：终端没有 ANSI 光标控制，只能 `\r` 回行首重打整行 + 空格擦掉旧尾 + `\b` 把光标退回 `cursor`。纯文本串口日志不解释 `\r`，所以日志里会出现 `DesOS> hDesOS> he...` 这类重复前缀，真实终端显示正常，不是 bug。
- **扩展键控制码取 0x80 以上**（`KEY_*`），避开 ASCII 可打印区与 `\b`/`\r`/`\n`；`char` 有符号，比较时必须转 `unsigned char`。
- **C.1c 的 IRQ12 路由必须晚于 `apic_init()`**：`apic_init()` 会先屏蔽全部 IOAPIC 引脚再重配，所以鼠标路由放在 `apic.c` 初始化里一起配（向量 44），而 8042 设备初始化 `mouse_init()` 放在 `b_selftest_run()` 之后。`pic_clear_mask(12)` 只服务 PIC 回退路径。
- **PS/2 包对齐要挡住 ACK**：命令应答 `0xFA` 也会经 IRQ12 到达；只用 `bit3 == 1` 判断会把它当包首字节。改用 `(b & 0xC8) == 0x08`（bit3=1、overflow 位为 0）即可。
- **不等 ACK**：`mouse_write()` 发完 `0xD4` + 命令就返回，不阻塞等 0xFA；QEMU 软件模拟下死等 ACK 会产生大量 VM exit 拖慢启动。
- **9 位补码位移**：`dx = pkt[1] - (flags & 0x10 ? 256 : 0)`、`dy = pkt[2] - (flags & 0x20 ? 256 : 0)`；X/Y overflow 位（0x40/0x80）置位时忽略本次位移。
- **C.2 帧缓冲只支持 32bpp**：`fb_ready()` 同时检查 `g_fb != NULL` 与 `bpp == 32`，所有写入都先过它，`fb_init()` 之前的早期 `kprintf` 才会安全跳过；非 32bpp 时 `kernel_main` 直接 `halt()`。
- **`fb_rgb` 必须走帧缓冲掩码**：不要硬编码 `0xRRGGBB`，不同固件的 `red/green/blue_mask_shift` 可能不同；拼色与自检读回都用同一个 `fb_rgb` 值比较。
- **控制台滚动靠 `kmemmove` 搬显存**：满屏时把控制台区域逐像素行上移一个字符高度、再清最后一行。控制台坐标独立（M1 大字在 y=80，日志从 y=150 起），两者不互相覆盖。
- **别在 `fb_console_putc` 里调 `kprintf`**：`kputc` 已镜像到控制台，反向调用会无限递归；屏幕端只能用 `fb_fill_rect` / `fb_draw_char`。
- **C.2 让 B.7 的 tick 数变化**：控制台绘制让 VCPU 更忙，B.7 的 `ticks` 从 10 涨到 19、`switches=21`，但按 tick 等待的判据仍 PASS——这正是 B.7 修复要解决的 CPU 速度耦合问题。
- **PCI class code 偏移易错**：配置空间 0x08 是 `0x08=Revision / 0x09=ProgIF / 0x0A=SubClass / 0x0B=BaseClass`（小端 32 位）。初版写成 `(cls>>16)/(cls>>8)/(cls&0xFF)`，整条右移 8 位，VGA 报 `class 0000/02`、`vga: no`；正确是 `(cls>>24)/(cls>>16)/(cls>>8)`（Revision 在 `cls&0xFF`，不存）。
- **0xCF8/0xCFC 机制够用**：`outl(0xCF8, 0x80000000|bus<<16|dev<<11|fn<<8|(reg&0xFC))` 后 `inl(0xCFC)`；reg 必须 4 字节对齐，16/8 位在读出双字内移位。QEMU/SeaBIOS 默认支持。
- **枚举要跳过空 function**：vendor==0xFFFF 即无设备；header type bit7 标记多 function，否则只扫 function 0。
