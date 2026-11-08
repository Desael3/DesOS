#!/usr/bin/env python3
"""build_kernel.py —— DesOS 内核构建脚本（对应 ProjectRoad A.5）

流程：
  1. nasm 把 kernel/asm/boot.asm 汇编成 ELF64 目标文件。
  2. x86_64-elf-gcc 把 kernel/src/*.c 编译成目标文件
     （关键旗标：-mcmodel=kernel -mno-red-zone，因内核载入更高半 >4GB）。
  3. 用 kernel/kernel.ld 链接成 kernel.elf，落到 build/kernel.elf。

运行环境：在 WSL 中执行（交叉工具链在 ~/opt/cross/bin，已加入 PATH）。
用法：
  wsl bash -c "python3 /mnt/e/Mine/A_Desael3/DesOS/tools/build_kernel.py"
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIMINE_INC = os.path.join(ROOT, "tools", "limine", "limine-12.9.6",
                          "limine-protocol", "include")
ASM_DIR = os.path.join(ROOT, "kernel", "asm")
SRC_DIR = os.path.join(ROOT, "kernel", "src")
INC_DIR = os.path.join(ROOT, "kernel", "inc")
LD_SCRIPT = os.path.join(ROOT, "kernel", "kernel.ld")
BUILD_DIR = os.path.join(ROOT, "build")
OUT = os.path.join(BUILD_DIR, "kernel.elf")

C_FLAGS = [
    "-ffreestanding", "-mcmodel=kernel", "-mno-red-zone", "-fno-pic",
    "-fno-stack-protector", "-fno-builtin", "-Wall", "-Wextra", "-c",
    "-I" + LIMINE_INC, "-I" + INC_DIR,
]


def find_tool(name, prefix="x86_64-elf-"):
    # 优先用带前缀的交叉工具（内核绝不能用主机 gcc 链接）。
    p = shutil.which(prefix + name)
    if p:
        return p
    # 常见交叉工具链安装位置（非交互 shell 可能没把 ~/opt/cross/bin 放进 PATH）
    home = os.path.expanduser("~")
    p = os.path.join(home, "opt", "cross", "bin", prefix + name)
    if os.path.exists(p):
        return p
    return None


def run(tool, args):
    cmd = [tool] + args
    print(">>", " ".join(cmd), flush=True)
    r = subprocess.run(cmd)
    if r.returncode != 0:
        print(f"ERROR: {tool} 失败（rc={r.returncode}）", file=sys.stderr)
        sys.exit(r.returncode)


def main():
    nasm = find_tool("nasm", prefix="")
    gcc = find_tool("gcc")
    if not nasm:
        print("ERROR: 找不到 nasm", file=sys.stderr)
        sys.exit(1)
    if not gcc:
        print("ERROR: 找不到 x86_64-elf-gcc（请确认交叉工具链在 PATH 或 ~/opt/cross/bin）",
              file=sys.stderr)
        sys.exit(1)

    os.makedirs(BUILD_DIR, exist_ok=True)

    # 0a) D.4：从单一数据源生成 CLI 命令表 / help 文档 / 命令手册，
    #     保证 kernel/inc/cli_commands.h 与 docs/ 永不漂移（生成失败即中止构建）。
    run(sys.executable, [os.path.join(ROOT, "tools", "gen_cli_docs.py")])

    # 0) B.10：先编译用户态程序，生成 build/user_image.asm（incbin ELF 镜像）
    run(sys.executable, [os.path.join(ROOT, "tools", "build_user.py")])

    # 1) 汇编 kernel/asm/*.asm + 生成的内嵌镜像 asm
    #    文件名排序保证 boot.asm 排在首位；B.4/B.5 起新增 gdt.asm、idt_stubs.asm。
    asm_srcs = [os.path.join(ASM_DIR, f)
                for f in sorted(os.listdir(ASM_DIR)) if f.endswith(".asm")]
    image_asm = os.path.join(BUILD_DIR, "user_image.asm")
    if os.path.exists(image_asm):
        asm_srcs.append(image_asm)

    asm_objs = []
    for src in asm_srcs:
        # 加 asm_ 前缀：避免与同名 C 源（如 gdt.c / gdt.asm）的产物互相覆盖
        obj = os.path.join(BUILD_DIR,
                           "asm_" + os.path.splitext(os.path.basename(src))[0] + ".o")
        run(nasm, ["-f", "elf64", src, "-o", obj])
        asm_objs.append(obj)

    # 2) 编译所有 C 源文件
    c_objs = []
    for f in sorted(os.listdir(SRC_DIR)):
        if not f.endswith(".c"):
            continue
        cpath = os.path.join(SRC_DIR, f)
        obj = os.path.join(BUILD_DIR, os.path.splitext(f)[0] + ".o")
        run(gcc, C_FLAGS + [cpath, "-o", obj])
        c_objs.append(obj)

    # 3) 链接（所有 ASM 目标 + 所有 C 目标）
    run(gcc, ["-T" + LD_SCRIPT, "-nostdlib", "-ffreestanding"]
        + asm_objs + c_objs + ["-o", OUT])

    print(f"OK: 已生成 {OUT}")


if __name__ == "__main__":
    main()
