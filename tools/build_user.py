#!/usr/bin/env python3
"""build_user.py —— 编译用户态程序并生成内核内嵌镜像（对应 ProjectRoad B.10）

流程：
  1. x86_64-elf-gcc 把 user/*.c 编成静态 ELF64（无 libc、无 PIE，入口 _start）。
  2. 生成 build/user_image.asm，用 NASM 的 incbin 把 ELF 原样放进内核 .rodata。
     C 侧通过 user_elf_start / user_elf_end 两个符号访问（见 kernel/src/elf.c）。

由 tools/build_kernel.py 在编译内核前自动调用；也可在 WSL 中单独执行。
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
USER_DIR = os.path.join(ROOT, "user")
BUILD_DIR = os.path.join(ROOT, "build")
OUT_DIR = os.path.join(BUILD_DIR, "user")
ELF = os.path.join(OUT_DIR, "hello.elf")
LD_SCRIPT = os.path.join(USER_DIR, "linker.ld")
IMAGE_ASM = os.path.join(BUILD_DIR, "user_image.asm")


def find_gcc():
    p = shutil.which("x86_64-elf-gcc")
    if p:
        return p
    p = os.path.join(os.path.expanduser("~"), "opt", "cross", "bin", "x86_64-elf-gcc")
    return p if os.path.exists(p) else None


def main():
    gcc = find_gcc()
    if not gcc:
        print("ERROR: 找不到 x86_64-elf-gcc", file=sys.stderr)
        sys.exit(1)

    os.makedirs(OUT_DIR, exist_ok=True)
    cmd = [
        gcc, "-ffreestanding", "-fno-builtin", "-fno-pic", "-fno-pie",
        "-no-pie", "-m64", "-static", "-nostdlib",
        "-Wl,-T," + LD_SCRIPT,
        os.path.join(USER_DIR, "hello.c"), "-o", ELF,
    ]
    print(">>", " ".join(cmd), flush=True)
    r = subprocess.run(cmd)
    if r.returncode != 0:
        print("ERROR: 用户程序编译失败", file=sys.stderr)
        sys.exit(r.returncode)

    # incbin 用绝对路径，避免 NASM 相对当前工作目录找不到文件
    asm = (
        "bits 64\n"
        "section .rodata\n"
        "align 8\n"
        "global user_elf_start\n"
        "global user_elf_end\n"
        "user_elf_start:\n"
        f'incbin "{ELF}"\n'
        "user_elf_end:\n"
    )
    with open(IMAGE_ASM, "w", newline="\n") as f:
        f.write(asm)
    print(f"OK: {ELF} ({os.path.getsize(ELF)} bytes) -> {IMAGE_ASM}")


if __name__ == "__main__":
    main()
