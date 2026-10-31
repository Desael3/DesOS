# DesOS 项目路线图（Project Roadmap）

> 操作系统名称：**DesOS**
> 当前阶段目标：**先做电脑（x86_64 架构）**
> 文档定位：从「下载第一个工具」开始，把每一步拆到最小，便于逐条执行与核对。

---

## 0. 目标与范围（先对齐，再动手）

### 0.1 用户视角的最终目标（暂定）
1. **软件兼容**：能运行 Windows 的 `EXE / BAT / CMD / MSI`、Android 的 `APK`、Linux 的 `DEB` 及其安装后的程序。
2. **UI**：类似 Windows 但更「圆滑/软」的界面风格。
3. **CLI**：类似 Linux 的命令行，但说明更清晰、更友好。
4. **硬件性能**：与其他系统一致，主要取决于设备自身硬件。
5. **省电**：笔记本上比 Windows 更省电（更好的空闲调度与电源状态管理）。
6. **更新体验**：安全更新不烦人（静默、可计划、可回滚）。
7. **用户模型**：区分 `user / sudo-user / system-user / kernel-user` 并配置对应权限。
8. **自有生态**：有自己的软件/程序后缀名，并自动区分 `/` 与 `\` 路径。

### 0.2 必须提前说清楚的真相（重要）
- 目标 1 相当于同时做 **ReactOS（Windows 兼容）+ Android 运行时 + Debian 兼容层**，这是世界级工程，单人或小团队需要以「年」为单位推进。
- 因此本路线图采用 **「内核自研 + 用户态兼容层（personality servers）」** 的架构，把最难的部分（Win32/Android/Linux 兼容）放到用户态逐步攻克，而非一上来就重写整个生态。
- **程序栈由我（按你的授权）决定**，见第 2 节。

### 0.3 阶段优先级（先跑起来，再谈兼容）
```
阶段 A：能启动、能显字、能进 shell        （内核 + 引导 + 最简驱动）
阶段 B：有图形、有窗口、有软 UI            （显示/输入/窗口管理器）
阶段 C：自有软件生态（.desos 包 + 路径规则）
阶段 D：Linux 兼容层（先跑 DEB / 命令行程序）
阶段 E：Windows 兼容层（Wine 思路跑 EXE）
阶段 F：Android 兼容层（跑 APK，需要 Linux 内核 ABI 子集）
阶段 G：省电 / 更新 / 权限模型打磨
```
> 兼容层的顺序是现实考量：**先 Linux（最难的内核 ABI 提前打底，Android 也依赖它），再 Windows（用户态翻译），最后 Android（需要前两者）。**

---

## 1. 环境准备：工具下载与安装（最小步骤）

> 前提：你的宿主机是 **Windows（win32 / PowerShell）**，所以采用
> **Windows 11 + WSL2 (Ubuntu 24.04)** 作为开发环境，再用 **QEMU** 在本机虚拟化测试。
> 每一步只做一件事。

### 1.1 安装 Linux 子系统和基础环境
- [x] **步骤 1**：打开 Microsoft Store，搜索并安装 **Ubuntu 24.04 LTS**。（已完成：`Ubuntu-24.04` 已注册并 Running）
- [x] **步骤 2**：启动 Ubuntu 24.04，按提示设置用户名和密码，完成首次初始化。（已完成：用户 `desael3`，`HOME=/home/desael3`）
- [x] **步骤 3**：在 Ubuntu 终端执行 `sudo apt update`（更新软件源索引）。（已完成）
- [x] **步骤 4**：执行 `sudo apt upgrade -y`（升级已装包到最新）。（已完成：0 待升级）
- [x] **步骤 5**：执行 `sudo apt install -y git`（安装版本控制工具 git）。（已完成：2.43.0）
- [x] **步骤 6**：执行 `sudo apt install -y curl`（安装下载工具 curl）。（已完成：8.5.0）
- [x] **步骤 7**：执行 `sudo apt install -y wget`（安装备用下载工具 wget）。（已完成：1.21.4）
- [x] **步骤 8**：执行 `sudo apt install -y build-essential`（安装 GCC/Make 等基础编译工具）。（已完成：gcc 13.3.0）
- [x] **步骤 9**：执行 `sudo apt install -y cmake`（安装 CMake 构建工具）。（已完成：3.28.3）

### 1.2 安装 Rust 工具链（内核主语言）
> 说明：经检查，Windows 侧已存在 Rust（`rustc 1.99.1` / `cargo 1.99.1` / `rustup 1.29.1`），WSL 内无。按「已有则不重复安装」原则，**直接复用 Windows 工具链**，内核构建改为在 Windows 侧进行（无需在 WSL 内再装一份）。
- [x] **步骤 10**：下载并运行 rustup 安装脚本。（已完成：Windows 已装 rustup 1.29.1）
- [x] **步骤 11**：在提示中选 `1) Default installation`。（已完成）
- [x] **步骤 12**：`source "$HOME/.cargo/env"` / 确保 PATH 含 cargo。（已完成：Windows PATH 已含）
- [x] **步骤 13**：`rustup toolchain install nightly`。（已完成：nightly 已装）
- [x] **步骤 14**：固定 nightly 工具链。（已完成：改用项目内 `rust-toolchain.toml` 指定 `channel = "nightly"`，不改全局默认，保留 stable 用于其它项目）
- [x] **步骤 15**：`rustup component add rust-src`。（已完成，已加至 nightly）
- [x] **步骤 16**：`rustup target add x86_64-unknown-none`。（已完成，已加至 nightly）
- [x] **步骤 17**：`rustup component add llvm-tools-preview`。（已完成，已加至 nightly）
- [x] **步骤 18**：`cargo install bootimage`。（已完成：bootimage 0.10.5）
> 注：原后台安装被手动终止后，由用户手动按「项目级 `rust-toolchain.toml`」方案补齐 14/16/17/18。验证：`rustc` 项目内为 1.101.0-nightly；`x86_64-unknown-none` 目标、`llvm-tools`、`rust-src` 均已就位；`bootimage 0.10.5` 可用。

### 1.3 安装汇编器与引导相关工具
- [x] **步骤 19**：执行 `sudo apt install -y nasm`（安装 NASM，用于写早期引导汇编）。（已完成：NASM 2.16.01，WSL 内已是最新）
- [x] **步骤 20**：执行 `sudo apt install -y xorriso`（安装 ISO 镜像制作工具）。（已完成：xorriso 1.5.6）
- [x] **步骤 21**：执行 `sudo apt install -y mtools`（安装 FAT 镜像工具，便于做启动盘）。（已完成：mtools 4.0.43）
> 注：nasm/xorriso/mtools 均装在 **WSL(Ubuntu)**。由于内核用 Windows 侧 Rust 编译、ISO 用 WSL 侧 xorriso/mtools 打包，阶段 A 需注意把 Windows 编译出的内核二进制传入 WSL 再打包（或后续统一构建环境）。
- [x] **步骤 22**：下载最新 **limine-*.tar.xz**。（已完成：v12.9.6，用 `wget` 拉取并解压至 WSL `/tmp`）
- [x] **步骤 23**：在 Ubuntu 中解压并执行 `./configure && make`。（已完成：Limine 12.9.6 编译成功）
- [x] **步骤 24**：执行 `sudo make install`。（已完成：`/usr/local/bin/limine` 可用，`limine --version` = 12.9.6）

### 1.4 安装模拟器（用于本机测试内核）
- [x] **步骤 25**：在 Windows 侧下载并安装 **QEMU for Windows**，安装到 **D 盘**并加入系统 **PATH**。（已完成：用户手动安装至 `D:\Program Files\qemu`，QEMU 11.1.0）
- [x] **步骤 26**：【已确认】**不**在 WSL/Ubuntu 内安装 QEMU。原因：WSL 暂不支持窗口显示，图形化测试无法出窗；统一使用 **Windows 版 QEMU** 来启动内核 ISO（GUI 测试在 Windows 侧进行）。若仅做无头(headless)验证，仍用 Windows 版并加 `-display none` 参数。
- [x] **步骤 27**：在 **Windows 的 PowerShell**（非 WSL）中验证：`qemu-system-x86_64 --version`。（已完成：QEMU emulator version 11.1.0）

### 1.5 安装辅助脚本与编辑器
- [x] **步骤 28**：Python（构建/打包脚本用）。（已完成：Windows 已装 **Python 3.11.9**，无需在 WSL 重复安装；若阶段 A 的 WSL 脚本需要 python3 再补装）
- [x] **步骤 29**：`ripgrep` + `fd-find`。（已完成：WSL 内 `rg 14.1.0` / `fdfind 9.0.0`）
- [x] **步骤 30**：Windows 侧 **VS Code** + **Remote - WSL** 扩展。（已完成：VS Code 1.144.0 位于 `D:\Programs\Microsoft VS Code`，Remote-WSL v1.104.7 已装）
- [x] **步骤 31**：VS Code 扩展 **rust-analyzer**。（已完成：v0.3.3097 已装）

### 1.6 建立代码仓库
- **步骤 32**：在 GitHub 新建仓库 **DesOS**（私有或公开自选）。
- **步骤 33**：在 Ubuntu 执行 `mkdir -p ~/desos && cd ~/desos && git init`（本地建仓）。
- **步骤 34**：执行 `git remote add origin <你的仓库地址>`（关联远程）。
- **步骤 35**：执行 `git commit --allow-empty -m "init desos"` 然后 `git push -u origin main`（完成首次提交）。

### 1.7 安装持续集成（可选但强烈建议）
- **步骤 36**：在仓库根目录新建 `.github/workflows/build.yml`，内容写「每次 push 自动用 Ubuntu + Rust nightly 编译内核」。
- **步骤 37**：把该文件提交并 push，到 GitHub Actions 页面确认首次 CI 跑绿。

> ✅ 完成 1.1–1.7 后，你的「工具箱」已就绪：能写 Rust 裸机内核、能用 NASM 写引导、能用 Limine 引导、能用 QEMU 测试、能用 git 管理。

---

## 2. 程序栈与技术选型（我已替你决定）

| 层级 | 选型 | 理由 |
|---|---|---|
| 引导 | **Limine** | 现代、轻量、文档友好，支持 x86_64 裸机内核 |
| 内核语言 | **Rust（`#![no_std]`）+ 少量 x86_64 汇编** | 内存安全，减少内核级漏洞 |
| 内核模型 | **混合内核（宏内核骨架 + 用户态 personality 服务）** | 兼顾性能与兼容层隔离 |
| 目标架构 | `x86_64-unknown-none`（裸机） | 电脑优先 |
| 构建 | `cargo` + 自定义 `build.rs` + `bootimage` | 自动化生成可启动镜像 |
| 显示 | 先 **VESA/线性帧缓冲（framebuffer）**，后接 GPU 驱动 | 先能画，再优化 |
| 窗口/UI | 自研 **合成器（compositor）+ 软圆角控件库** | 实现「圆滑/软」风格 |
| 字体 | **FreeType** 移植到内核/用户态 | 清晰文本渲染 |
| 文件系统 | 自有 **DesFS**（同时兼容 FAT32/ext2 读写） | 自带生态 + 互通 |
| CLI | 自研 **`dsh`**（DesOS Shell），带 `--help` 友好说明 | 目标 3 |
| 软件包 | 自有 **`.desos`** 包格式 + `dpkg` 思路的 `dpm` 包管理器 | 目标 8 |
| 兼容层 | **Linux ABI 子集 → Wine 思路的 Win32 → Android Runtime 子集** | 目标 1，分阶段 |
| 电源 | **ACPI + CPU 空闲状态(C-states)/节能调度** | 目标 5 |
| 更新 | **A/B 双分区 + 静默后台 + 可回滚** | 目标 6 |
| 权限 | **Capabilities + 四档用户模型** | 目标 7 |

---

## 3. 阶段 A：让内核「启动并说话」（最小可运行系统）

> 目标：Limine 引导 → 进入 Rust 内核 → 在屏幕上打印 "Hello, DesOS" → 能用键盘输入、能在 QEMU 里跑。

- **步骤 A1**：在 `~/desos` 下用 `cargo new --bin kernel` 建立内核 crate。
- **步骤 A2**：修改 `kernel/Cargo.toml`，加入 `#![no_std]`、`#![no_main]` 与 `panic_handler`。
- **步骤 A3**：写 `kernel/src/main.rs` 的 `_start` 入口（Limine 约定的栈帧）。
- **步骤 A4**：用 NASM 写 `boot.asm`：设置栈、长模式（64 位）、调用 Rust 的 `kmain`。
- **步骤 A5**：实现最简 **VGA 文本/帧缓冲打印** 函数（先输出一行字）。
- **步骤 A6**：写 `limine.conf` 指向内核镜像，并用 `bootimage`/`xorriso` 打成 ISO。
- **步骤 A7**：用 QEMU 启动 ISO，确认看到 "Hello, DesOS"。
- **步骤 A8**：实现 **PS/2 键盘驱动**，把按键回显到屏幕（最简 shell 雏形）。
- **步骤 A9**：建立 `Makefile`/`build.sh` 一键 `build + run`。

> ✅ 阶段 A 完成标准：QEMU 里能看到内核输出，并能用键盘敲字符。

---

## 4. 阶段 B：图形、窗口与「软 UI」

- **步骤 B1**：初始化 **帧缓冲（framebuffer）** 图形模式（如 1920×1080）。
- **步骤 B2**：实现基础 **2D 绘图原语**（画点/线/矩形/圆角矩形/填充）。
- **步骤 B3**：移植 **FreeType**，能渲染 TTF 字体。
- **步骤 B4**：实现 **窗口管理器/compositor**（窗口、标题栏、圆角、阴影）。
- **步骤 B5**：实现 **鼠标驱动**（USB/PS2），支持拖动窗口。
- **步骤 B6**：做一套 **「软 UI」控件库**（圆角按钮、柔和阴影、半透明、动画过渡）。
- **步骤 B7**：实现 **桌面环境 DesDE**（任务栏、开始菜单雏形、通知区）。
- **步骤 B8**：把 CLI（`dsh`）做成一个**全屏/浮动终端窗口**，而非纯文本模式。

> ✅ 阶段 B 完成标准：能看到带圆角窗口的桌面，可拖动窗口，能在终端窗口里敲命令。

---

## 5. 阶段 C：自有软件生态（.desos 与路径规则）

- **步骤 C1**：定义 **`.desos` 包格式**（元数据 + 可执行 + 资源），用 `tar.zst` 容器。
- **步骤 C2**：写 **`dpm`（DesOS Package Manager）**：安装/卸载/查询/升级。
- **步骤 C3**：实现 **路径抽象层**：内核与 shell 同时识别 `/` 与 `\`，统一归一化为内部表示（目标 8）。
- **步骤 C4**：定义 **可执行后缀**：如 `.dexec`（原生）、`.dsh`（脚本）、`.desos`（打包应用）。
- **步骤 C5**：做一个 **软件中心** UI（基于阶段 B 的控件库）。
- **步骤 C6**：用 `dpm` 发布 3 个示例程序（计算器/记事本/终端），验证完整闭环。

---

## 6. 阶段 D：Linux 兼容层（先打通 DEB 与命令行）

- **步骤 D1**：实现 **Linux 系统调用子集**（在用户态做 syscall 翻译层）。
- **步骤 D2**：移植 **`musl`/部分 `glibc`** 到该层，让 Linux 二进制能动态链接。
- **步骤 D3**：移植 **`dpkg` + `apt`** 思路，能解析并安装 `.deb` 包。
- **步骤 D4**：先跑通 **Linux CLI 工具**（如 `coreutils` 子集、`bash`）。
- **步骤 D5**：验证 1–2 个常见 DEB 包安装并可运行。

> 说明：Android（阶段 F）也依赖 Linux ABI，所以这一步是地基。

---

## 7. 阶段 E：Windows 兼容层（EXE / BAT / CMD / MSI）

- **步骤 E1**：实现 **PE 加载器**（解析 EXE/DLL 头、重定位、导入表）。
- **步骤 E2**：实现 **Win32 API 子集**（窗口、GDI、文件、进程、注册表桩）。
- **步骤 E3**：移植/借鉴 **Wine** 的用户态实现（作为兼容层，而非重写）。
- **步骤 E4**：实现 **BAT/CMD 解释器**（命令解释 + 流程控制）。
- **步骤 E5**：实现 **MSI 安装器**（解析 Windows Installer 数据库并映射到 `dpm`/DesFS）。
- **步骤 E6**：先跑通几个简单 EXE（如记事本类、命令行工具），再逐步扩大覆盖。

---

## 8. 阶段 F：Android 兼容层（APK）

- **步骤 F1**：在阶段 D 的 Linux ABI 之上，实现 **Android 特有系统调用/服务（binder、ashmem 等）**。
- **步骤 F2**：移植 **ART（Android Runtime）** 或兼容的 Dalvik 执行环境。
- **步骤 F3**：实现 **APK 安装器**（解包、签名校验、映射到 `dpm`）。
- **步骤 F4**：实现 **Android 界面桥接**（把 Android View 渲染到 DesOS 合成器）。
- **步骤 F5**：先跑通 1–2 个简单 APK。

---

## 9. 阶段 G：省电、更新与权限模型打磨

- **步骤 G1（权限，目标 7）**：
  - 定义四档：`kernel-user`（仅内核态）、`system-user`（系统服务）、`sudo-user`（可提权的管理员）、`user`（普通用户）。
  - 实现 **Capabilities** 细粒度授权，而非简单的 root/非 root。
- **步骤 G2（省电，目标 5）**：
  - 实现 **ACPI** 解析与 **C-states / P-states** 调度。
  - 空闲时把 CPU 降频/进入深闲置；笔记本续航优先策略。
- **步骤 G3（更新，目标 6）**：
  - **A/B 双分区**：更新写入备用分区，重启切换，失败自动回滚。
  - **静默后台下载 + 可计划安装时段**，不打断用户。
- **步骤 G4（体验）**：统一错误处理文案、清晰 CLI 帮助、无障碍（高对比/大字号）。

---

## 10. 里程碑检查清单（快速对照）

| 里程碑 | 完成标志 |
|---|---|
| 工具就绪 | 1.1–1.7 全部执行成功，QEMU 可启动 |
| 阶段 A | 内核打印字符 + 键盘回显 |
| 阶段 B | 圆角窗口桌面 + 可拖动 + 终端窗口 |
| 阶段 C | `.desos` 包能装能跑 + `/` `\` 通吃 |
| 阶段 D | 能装并运行 DEB / Linux 命令行 |
| 阶段 E | 能运行简单 EXE / BAT / MSI |
| 阶段 F | 能安装并运行简单 APK |
| 阶段 G | 四档权限 + 省电 + 静默更新 |

---

## 11. 风险与建议（务必看）

1. **兼容层是无限深坑**：Windows/Android 兼容是「永远做不完」的。建议每个阶段都设「能跑通 1–2 个真实程序」的小目标，而不是追求 100% 覆盖。
2. **先 Linux 后 Windows 的架构更省力**：因为 Android 依赖 Linux ABI；若顺序颠倒会返工。
3. **尽早虚拟化测试**：每写完一个模块就用 QEMU 跑，不要攒到后期。
4. **文档与测试并重**：内核没有「重启试试」的容错，单测和 CI 能救命。
5. **可参考的现成项目**（学习用，非照搬）：`Redox OS`（Rust 内核）、`SerenityOS`（类 Win 软 UI）、`ReactOS`（Win 兼容）、`Wine`（Win 用户态）、`Limine`（引导）。

---

> 下一步行动：从 **步骤 1**（安装 Ubuntu 24.04 LTS）开始，逐条打到对勾即可。每完成一节，回来更新本文档顶部的进度标记。
