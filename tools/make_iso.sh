#!/usr/bin/env bash
# make_iso.sh —— DesOS 可启动 ISO 打包入口（对应 ProjectRoad A.6）
#
# 封装两步：[1] 用 build_kernel.py 交叉编译内核 -> build/kernel.elf
#           [2] 用 build_iso.py 把内核 + limine.conf + Limine 引导文件打进 build/DesOS.iso
#
# 用法（在 WSL 中执行）：
#   wsl bash -c "bash /mnt/e/Mine/A_Desael3/DesOS/tools/make_iso.sh"
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"

echo "==> [1/2] 构建内核"
python3 "$SCRIPT_DIR/build_kernel.py"

echo "==> [2/2] 打包 ISO"
python3 "$SCRIPT_DIR/build_iso.py"

echo "OK: ISO 已生成于 $ROOT/build/DesOS.iso"
