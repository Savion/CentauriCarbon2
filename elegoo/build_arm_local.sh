#!/bin/bash
###############################################################################
# ARM build using the Elegoo/Sunxi prebuilt toolchain (GCC 6.4.1, glibc 2.23)
# Targets the AllWinner R528 (armhf) running on the printer
###############################################################################

TOOLCHAIN_ROOT="/opt/toolchains/prebuilt/gcc/linux-x86/arm/toolchain-sunxi-glibc/toolchain"
TOOLCHAIN_BIN="$TOOLCHAIN_ROOT/bin"

export CC="$TOOLCHAIN_BIN/arm-openwrt-linux-gnueabi-gcc"
export CXX="$TOOLCHAIN_BIN/arm-openwrt-linux-gnueabi-g++"
# Required by the OpenWrt wrapper script
export STAGING_DIR="$TOOLCHAIN_ROOT"

BUILD_DIR=build_arm
rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR
cd $BUILD_DIR

cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_CXX_COMPILER="$CXX"

if [ $? -ne 0 ]; then echo "CMake configure failed."; exit 1; fi

make elegoo_printer -j$(nproc)

if [ $? -eq 0 ]; then
    echo ""
    echo "=== Build succeeded ==="
    file elegoo_printer
    echo "Size: $(du -h elegoo_printer | cut -f1)"
    echo "GLIBC requirements:"
    objdump -p elegoo_printer | grep GLIBC | sort -V
else
    echo "Build failed."
    exit 1
fi
cd ..
