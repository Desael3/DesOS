#!/usr/bin/env python3
"""build_iso.py —— DesOS 可启动 ISO 打包脚本（对应 ProjectRoad A.6）

流程（依据 Limine 12.9.x 的「BIOS/UEFI hybrid ISO creation」；工作目录只保留
`limine-12.9.6/bin/` 与 `limine-protocol/include/limine.h`，完整源码见
`tools/limine/limine-12.9.6.tar.xz`）：
  1. 准备暂存根目录 iso_root，放入：
       - limine.conf（项目根）
       - limine-bios.sys / limine-bios-cd.bin / limine-uefi-cd.bin（来自 Limine bin/）
       - kernel.elf（A.5 产物，放根目录，匹配 limine.conf 的 boot():/kernel.elf）
       - EFI/BOOT/BOOTX64.EFI（来自 Limine bin/）
  2. xorriso 生成混合 BIOS/UEFI 可启动 ISO。
  3. limine bios-install 写入 BIOS 引导代码。
产物：build/DesOS.iso

前置：先跑 tools/build_kernel.py 生成 build/kernel.elf。
运行：wsl bash -c "python3 /mnt/e/Mine/A_Desael3/DesOS/tools/build_iso.py"
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIMINE_BIN = os.path.join(ROOT, "tools", "limine", "limine-12.9.6", "bin")
BUILD_DIR = os.path.join(ROOT, "build")
ISO_ROOT = os.path.join(BUILD_DIR, "iso_root")
KERNEL_ELF = os.path.join(BUILD_DIR, "kernel.elf")
LIMINE_CONF = os.path.join(ROOT, "limine.conf")
ISO_OUT = os.path.join(BUILD_DIR, "DesOS.iso")


def run(cmd):
    print(">>", " ".join(cmd), flush=True)
    r = subprocess.run(cmd)
    if r.returncode != 0:
        print(f"ERROR: {' '.join(cmd)} 失败（rc={r.returncode}）", file=sys.stderr)
        sys.exit(r.returncode)


def need(path, what):
    if not os.path.exists(path):
        print(f"ERROR: 缺少 {what}：{path}\n"
              f"       请先运行 tools/build_kernel.py 生成内核。", file=sys.stderr)
        sys.exit(1)


def main():
    xorriso = shutil.which("xorriso")
    if not xorriso:
        print("ERROR: 找不到 xorriso（ISO 打包必需）", file=sys.stderr)
        sys.exit(1)

    limine_tool = os.path.join(LIMINE_BIN, "limine")
    need(limine_tool, "Limine 工具")
    need(KERNEL_ELF, "kernel.elf")
    need(LIMINE_CONF, "limine.conf")

    # BIOS 引导镜像可能比约定值大（如 limine-bios-cd.bin 28672 字节 = 56 扇区），
    # 必须按实际大小设置 -boot-load-size，否则 SeaBIOS 只加载截断部分导致引导失败。
    bios_cd = os.path.join(LIMINE_BIN, "limine-bios-cd.bin")
    boot_load_size = (os.path.getsize(bios_cd) + 511) // 512

    # 1) 准备 iso_root
    if os.path.isdir(ISO_ROOT):
        shutil.rmtree(ISO_ROOT)
    os.makedirs(os.path.join(ISO_ROOT, "EFI", "BOOT"))

    # limine.conf 常来自 Windows（CRLF）。Limine 把 \r 当作值的一部分，会破坏
    # 选项解析（例如 timeout 变成 "0\r"）。统一转成 LF 再写入 ISO。
    with open(LIMINE_CONF, "r", encoding="utf-8", newline="") as f:
        conf_text = f.read().replace("\r\n", "\n").replace("\r", "\n")
    with open(os.path.join(ISO_ROOT, "limine.conf"), "w",
              encoding="utf-8", newline="\n") as f:
        f.write(conf_text)
    shutil.copy(os.path.join(LIMINE_BIN, "limine-bios.sys"),
                os.path.join(ISO_ROOT, "limine-bios.sys"))
    shutil.copy(os.path.join(LIMINE_BIN, "limine-bios-cd.bin"),
                os.path.join(ISO_ROOT, "limine-bios-cd.bin"))
    shutil.copy(os.path.join(LIMINE_BIN, "limine-uefi-cd.bin"),
                os.path.join(ISO_ROOT, "limine-uefi-cd.bin"))
    shutil.copy(KERNEL_ELF, os.path.join(ISO_ROOT, "kernel.elf"))
    shutil.copy(os.path.join(LIMINE_BIN, "BOOTX64.EFI"),
                os.path.join(ISO_ROOT, "EFI", "BOOT", "BOOTX64.EFI"))

    # 2) xorriso 生成混合 ISO
    run([
        xorriso, "-as", "mkisofs", "-R", "-r", "-J",
        "-b", "limine-bios-cd.bin",
        "-no-emul-boot", "-boot-load-size", str(boot_load_size), "-boot-info-table",
        "-hfsplus", "-apm-block-size", "2048",
        "--efi-boot", "limine-uefi-cd.bin",
        "-efi-boot-part", "--efi-boot-image", "--protective-msdos-label",
        ISO_ROOT, "-o", ISO_OUT,
    ])

    # 3) 写入 BIOS 引导代码
    run([limine_tool, "bios-install", ISO_OUT])

    print(f"OK: 已生成 {ISO_OUT}")


if __name__ == "__main__":
    main()
