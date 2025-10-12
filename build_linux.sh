#!/bin/bash

# Family mruby Linux native build script
# ESP-IDFが使用できない環境でのテスト用

echo "Building Family mruby for Linux (native)..."

# Create build directory
mkdir -p build

# Compile with mock ESP-IDF functions
gcc -o build/fmrb_linux_native -I. -DCONFIG_IDF_TARGET_LINUX=1 \
    -DFMRB_LINUX_NATIVE=1 \
    main/main_linux_native.c

if [ $? -eq 0 ]; then
    echo "Linux native build complete. Run with: ./build/fmrb_linux_native"
else
    echo "Build failed"
    exit 1
fi