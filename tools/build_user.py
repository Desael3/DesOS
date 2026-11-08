#!/usr/bin/env python3
"""build_user.py - 编译多个用户态程序并生成内核内嵌镜像清单（B.10 / D.1 / D.3b）

流程：
  1. x86_64-elf-gcc 把 user/apps/*.c 与 user/libc/*.c 分别编译成 .o；
     每个 app 与全部 libc .o 按 user/linker.ld 链接成独立静态 ELF64
     （无宿主 libc、无 PIE，入口 _start；-I kernel/inc 引共享 ABI 头）。
  2. 生成 build/user_image.asm：用 NASM incbin 把每个 ELF 放进内核 .rodata，
     并构造 manifest 表 userapp_table[]（name/start/end）+ userapp_table_len。
     hello 另外导出兼容符号 user_elf_start / user_elf_end（B.10 自检用）。
     C 侧经 kernel/inc/userapp.h 的 userapp_find() 按名字取镜像。

由 tools/build_kernel.py 在编译内核前自动调用；也可在 WSL 中单独执行。
"""
import glob
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
USER_DIR = os.path.join(ROOT, "user")
APPS_DIR = os.path.join(USER_DIR, "apps")
LIBC_DIR = os.path.join(USER_DIR, "libc")
KERNEL_INC = os.path.join(ROOT, "kernel", "inc")
BUILD_DIR = os.path.join(ROOT, "build")
OUT_DIR = os.path.join(BUILD_DIR, "user")
OBJ_DIR = os.path.join(OUT_DIR, "obj")
LD_SCRIPT = os.path.join(USER_DIR, "linker.ld")
IMAGE_ASM = os.path.join(BUILD_DIR, "user_image.asm")

CFLAGS = [
    "-ffreestanding", "-fno-builtin", "-fno-pic", "-fno-pie",
    "-fno-stack-protector", "-m64", "-nostdlib",
    "-I", LIBC_DIR, "-I", KERNEL_INC,
]


def find_gcc():
    p = shutil.which("x86_64-elf-gcc")
    if p:
        return p
    p = os.path.join(os.path.expanduser("~"), "opt", "cross", "bin", "x86_64-elf-gcc")
    return p if os.path.exists(p) else None


def run(cmd, what):
    print(">>", " ".join(cmd), flush=True)
    r = subprocess.run(cmd)
    if r.returncode != 0:
        print("ERROR: " + what + " failed", file=sys.stderr)
        sys.exit(r.returncode)


def make_manifest(entries):
    """entries: [(name, elf_path)] -> NASM 源码（含兼容符号与 manifest 表）"""
    lines = [
        "bits 64", "section .rodata", "align 8",
        "global userapp_table", "global userapp_table_len",
        "global user_elf_start", "global user_elf_end", "",
    ]
    for name, elf in entries:
        lines.append("align 8")
        if name == "hello":
            lines.append("user_elf_start:")
        lines.append("app_" + name + "_start:")
        lines.append('incbin "' + elf + '"')
        lines.append("app_" + name + "_end:")
        if name == "hello":
            lines.append("user_elf_end:")
        lines.append("")
    for name, _ in entries:
        lines.append('str_' + name + ': db "' + name + '", 0')
    lines += ["align 8", "userapp_table:"]
    for name, _ in entries:
        lines.append("  dq str_" + name + ", app_" + name + "_start, app_" + name + "_end")
    lines += ["userapp_table_len: dq " + str(len(entries)), ""]
    return "\n".join(lines)


def main():
    gcc = find_gcc()
    if not gcc:
        print("ERROR: x86_64-elf-gcc not found", file=sys.stderr)
        sys.exit(1)

    apps = sorted(glob.glob(os.path.join(APPS_DIR, "*.c")))
    libc_srcs = sorted(glob.glob(os.path.join(LIBC_DIR, "*.c")))
    if not apps:
        print("ERROR: no apps in user/apps/", file=sys.stderr)
        sys.exit(1)

    os.makedirs(OBJ_DIR, exist_ok=True)
    objs = {}
    for src in apps + libc_srcs:
        obj = os.path.join(OBJ_DIR, os.path.splitext(os.path.basename(src))[0] + ".o")
        run([gcc, *CFLAGS, "-c", src, "-o", obj], "compile " + src)
        objs[src] = obj

    entries = []
    for app in apps:
        name = os.path.splitext(os.path.basename(app))[0]
        elf = os.path.join(OUT_DIR, name + ".elf")
        link = [gcc, "-m64", "-static", "-no-pie", "-nostdlib",
                "-Wl,-T," + LD_SCRIPT, objs[app]]
        link += [objs[s] for s in libc_srcs]
        link += ["-o", elf]
        run(link, "link " + name)
        entries.append((name, elf))

    # hello 固定排第一，兼容符号 user_elf_start/end 才能包住它的镜像
    entries.sort(key=lambda e: (e[0] != "hello", e[0]))
    with open(IMAGE_ASM, "w", newline="\n") as f:
        f.write(make_manifest(entries))
    print("OK: " + str(len(entries)) + " apps -> " + IMAGE_ASM)
    for name, elf in entries:
        print("  " + name + ": " + str(os.path.getsize(elf)) + " bytes")


if __name__ == "__main__":
    main()