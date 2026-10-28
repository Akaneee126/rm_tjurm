#!/bin/sh
set -e
cd /root/smoke
echo "== opencv cflags =="
PKG_CONFIG_PATH=/usr/lib/aarch64-linux-gnu/pkgconfig pkg-config --cflags --libs opencv4
echo "== build =="
g++ -O2 -std=c++17 test2.cpp \
    -I/usr/local/include -L/usr/local/lib -laidlite \
    $(PKG_CONFIG_PATH=/usr/lib/aarch64-linux-gnu/pkgconfig pkg-config --cflags --libs opencv4) \
    -o test2 -lOpenCL -Wl,--allow-shlib-undefined
ls -l test2
echo "== run =="
LD_LIBRARY_PATH=/usr/local/lib timeout 120 ./test2 /root/models/0526_qcs6490_w8a8.qnn231.ctx.bin.aidem
echo "exit=$?"
