#!/usr/bin/env bash
# DesOS 交叉编译工具链构建：binutils + gcc (x86_64-elf)
# 按 promat 习惯 12：可见窗口 + 日志双保险（exec tee）
set -e
exec > >(tee /mnt/e/Mine/A_Desael3/DesOS/build_cross.log) 2>&1
echo "=== DesOS 交叉工具链构建开始 $(date) ==="

# 交互式 sudo（可见窗口里由你输入密码；免去 WSL 读 .lock 文件返回 0 字节的坑）
sudo apt-get install -y bison flex libgmp-dev libmpfr-dev libmpc-dev texinfo wget

PREFIX=$HOME/opt/cross
TARGET=x86_64-elf
export PATH="$PREFIX/bin:$PATH"
SRC=$HOME/src-cross
mkdir -p "$SRC"

if command -v x86_64-elf-gcc >/dev/null 2>&1; then
  echo "x86_64-elf-gcc 已存在，跳过构建"
  echo "=== 完成 $(date) ==="
  exit 0
fi

# ---- binutils ----
cd "$SRC"
[ -f binutils-2.42.tar.xz ] || wget -q https://ftp.gnu.org/gnu/binutils/binutils-2.42.tar.xz
rm -rf binutils-2.42 build-binutils
tar xf binutils-2.42.tar.xz
mkdir build-binutils && cd build-binutils
../binutils-2.42/configure --target=$TARGET --prefix=$PREFIX --with-sysroot --disable-nls --disable-werror
make -j"$(nproc)"
make install

# ---- gcc (bare metal, c/c++) ----
cd "$SRC"
[ -f gcc-13.3.0.tar.xz ] || wget -q https://ftp.gnu.org/gnu/gcc/gcc-13.3.0/gcc-13.3.0.tar.xz
rm -rf gcc-13.3.0 build-gcc
tar xf gcc-13.3.0.tar.xz
mkdir build-gcc && cd build-gcc
../gcc-13.3.0/configure --target=$TARGET --prefix=$PREFIX --disable-nls --enable-languages=c,c++ --without-headers
make -j"$(nproc)" all-gcc
make install-gcc

echo "=== 完成 $(date) ==="
echo "验证：x86_64-elf-gcc --version"
x86_64-elf-gcc --version | head -1
