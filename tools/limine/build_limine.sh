#!/usr/bin/env bash
# 工作目录清理后 limine-12.9.6/ 只保留构建必需文件（bin/ 与 limine-protocol/include/limine.h）。
# 需要重新编译 Limine 时，先从同目录的 limine-12.9.6.tar.xz 解压完整源码。
set -e
cd /mnt/e/Mine/A_Desael3/DesOS/tools/limine
if [ ! -f limine-12.9.6/configure ]; then
    echo "=== 解压 Limine 完整源码 ==="
    tar -xf limine-12.9.6.tar.xz
fi
cd limine-12.9.6
LOG=/mnt/e/Mine/A_Desael3/DesOS/tools/limine/rebuild.log
exec > >(tee "$LOG") 2>&1
echo "=== configure (bios+cd+uefi-x86-64+uefi-cd) ==="
./configure --prefix=/usr/local --enable-bios --enable-bios-cd --enable-uefi-x86-64 --enable-uefi-cd
echo "=== NO_BIOS check in config.h ==="
grep -i "NO_BIOS\|BUILD_BIOS" config.h 2>/dev/null || true
echo "=== make clean ==="
make clean || true
echo "=== make ==="
make -j"$(nproc)"
echo "=== built boot files ==="
ls -l bin/limine* 2>/dev/null
find . -maxdepth 2 -name "limine*.bin" -o -name "limine.sys" 2>/dev/null
echo "BUILD_DONE"
