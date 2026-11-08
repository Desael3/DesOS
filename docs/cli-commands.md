# DesOS CLI 命令手册

> 由 `tools/gen_cli_docs.py` 从 `COMMANDS` 规格表自动生成，请勿手改。
> 修改命令请改生成器后重新构建 ISO；`kernel/inc/cli_commands.h`、`docs/cli-help.txt` 同步更新。

## 约定

- 命令与参数用空格分隔，Enter 提交；支持 Backspace、方向键、Home/End/Delete（C.1b）。
- 任意命令后加 `--help` 或 `-h` 可查看该命令帮助；`help <command>` 等价。
- 别名由生成器统一维护，Shell 会自动规范化后再分发。
- `run [priv]` 的提权属于单用户控制台授权（操作者敲命令即确认），受 D.2 capability 矩阵约束。

## 快速清单

### 基础

| 命令 | 用法 | 说明 | 别名 |
|---|---|---|---|
| `help` | `help [command]` | 显示命令清单，或某条命令的详细帮助 | `?` |
| `echo` | `echo [text...]` | 回显一行文本 | - |
| `clear` | `clear` | 清空帧缓冲控制台 | - |
| `version` | `version` | 显示内核版本与阶段 | `uname`, `about` |
| `verbose` | `verbose [on|off]` | 切换屏幕日志镜像（串口日志始终完整） | - |

### 系统信息

| 命令 | 用法 | 说明 | 别名 |
|---|---|---|---|
| `mem` | `mem` | 物理内存总量与空闲量 | `free` |
| `ticks` | `ticks` | 内核启动以来的 PIT tick 数 | `uptime` |
| `pid` | `pid` | 当前主线程 id、名称与权限级 | - |
| `threads` | `threads` | 线程统计：创建数 / 上下文切换 / ring3 系统调用数 | `ps` |
| `whoami` | `whoami` | 显示当前身份（线程名、pid、权限级） | - |
| `services` | `services` | 列出服务注册表（名字、状态、归属权限） | `svc` |

### 用户程序

| 命令 | 用法 | 说明 | 别名 |
|---|---|---|---|
| `run` | `run [app] [priv]` | 加载并运行内嵌用户态 ELF 程序 | - |

### 硬件调试

| 命令 | 用法 | 说明 | 别名 |
|---|---|---|---|
| `mouse` | `mouse` | PS/2 鼠标坐标、按键、包与 IRQ12 计数 | - |

### 电源

| 命令 | 用法 | 说明 | 别名 |
|---|---|---|---|
| `halt` | `halt` | 停机（CPU hlt，不断电） | - |
| `poweroff` | `poweroff` | ACPI S5 关机 | - |
| `reboot` | `reboot` | 重启整机 | - |

## 命令详解

### help

- 分组：基础
- 用法：`help [command]`
- 别名：`?`
- 简述：显示命令清单，或某条命令的详细帮助

不带参数按分组列出全部命令；带命令名显示用法、别名与说明。任意命令后加 --help 或 -h 也可查看帮助。

示例：`help run`

### echo

- 分组：基础
- 用法：`echo [text...]`
- 别名：无
- 简述：回显一行文本

把参数原样输出到串口与帧缓冲控制台，常用于确认输入链路是否通畅。

示例：`echo DesOS B11 works`

### clear

- 分组：基础
- 用法：`clear`
- 别名：无
- 简述：清空帧缓冲控制台

清除屏幕上的历史输出，串口日志不受影响。

示例：`clear`

### version

- 分组：基础
- 用法：`version`
- 别名：`uname`, `about`
- 简述：显示内核版本与阶段

打印 DesOS 版本、体系结构与当前实现阶段。

示例：`version`

### verbose

- 分组：基础
- 用法：`verbose [on|off]`
- 别名：无
- 简述：切换屏幕日志镜像（串口日志始终完整）

默认静默启动：自检与命令演示不刷屏，只写 COM1 日志。on 之后内核输出重新镜像到帧缓冲屏幕，off 恢复静默；不带参数显示当前状态。

示例：`verbose on`

### mem

- 分组：系统信息
- 用法：`mem`
- 别名：`free`
- 简述：物理内存总量与空闲量

单位为 4KiB 页与 MiB；数据来自 B.1 物理页管理器（PMM）位图。

示例：`mem`

### ticks

- 分组：系统信息
- 用法：`ticks`
- 别名：`uptime`
- 简述：内核启动以来的 PIT tick 数

PIT 频率默认 100 Hz，tick 数除以频率约等于开机秒数。

示例：`ticks`

### pid

- 分组：系统信息
- 用法：`pid`
- 别名：无
- 简述：当前主线程 id、名称与权限级

主线程恒为 pid 0（kernel 权限）；用户程序由 run 以独立线程运行。

示例：`pid`

### threads

- 分组：系统信息
- 用法：`threads`
- 别名：`ps`
- 简述：线程统计：创建数 / 上下文切换 / ring3 系统调用数

用于验证 B.7 抢占式调度与 B.9 int 0x80 系统调用门。

示例：`threads`

### whoami

- 分组：系统信息
- 用法：`whoami`
- 别名：无
- 简述：显示当前身份（线程名、pid、权限级）

权限级四级：kernel / system / sudo / user（D.2 capability 模型）。

示例：`whoami`

### services

- 分组：系统信息
- 用法：`services`
- 别名：`svc`
- 简述：列出服务注册表（名字、状态、归属权限）

D.3 服务表：running 表示内核已拉起，stopped 多因当前环境无对应硬件；用户态经 SYS_SVC_INFO 读同一张表。

示例：`services`

### run

- 分组：用户程序
- 用法：`run [app] [priv]`
- 别名：无
- 简述：加载并运行内嵌用户态 ELF 程序

app 名来自构建期 userapp manifest（当前 hello、init），缺省 hello。priv 可选 user/system/sudo/kernel；控制台敲命令即 sudo 式授权，权限记录在 TCB 上并受 capability 校验。

示例：`run init system`

### mouse

- 分组：硬件调试
- 用法：`mouse`
- 别名：无
- 简述：PS/2 鼠标坐标、按键、包与 IRQ12 计数

移动鼠标后再次执行可观察坐标与 packets 增长，验证 C.1c IRQ12 链路。

示例：`mouse`

### halt

- 分组：电源
- 用法：`halt`
- 别名：无
- 简述：停机（CPU hlt，不断电）

进入 hlt 循环；与 poweroff 不同，ACPI 不参与。

示例：`halt`

### poweroff

- 分组：电源
- 用法：`poweroff`
- 别名：无
- 简述：ACPI S5 关机

从 FADT 动态解析 PM1a_CNT 与 DSDT _S5 的 SLP_TYP，写 (SLP_TYP<<10)|SLP_EN 真断电；端口在 QEMU / VMware UEFI / VMware BIOS 分别为 0x604 / 0x444 / 0x1004。

示例：`poweroff`

### reboot

- 分组：电源
- 用法：`reboot`
- 别名：无
- 简述：重启整机

优先使用 FADT RESET_REG，缺失时回退 8042 键盘控制器 (0xFE)。

示例：`reboot`

