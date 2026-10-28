#!/bin/sh
# usage: sh build_run.sh <name.cpp> [args...]
set -e
cd /root/smoke
SRC="$1"; shift || true
MODEL="${MODEL:-/root/models/0526_qcs6490_w8a8.qnn231.ctx.bin.aidem}"
export PKG_CONFIG_PATH=/usr/lib/aarch64-linux-gnu/pkgconfig
CFLAGS=$(pkg-config --cflags --libs opencv4)
g++ -O2 -std=c++17 "$SRC" -I/usr/local/include -L/usr/local/lib -laidlite \
    $CFLAGS -o "${SRC%.cpp}" -lOpenCL -Wl,--allow-shlib-undefined
echo "== run $SRC =="
LD_LIBRARY_PATH=/usr/local/lib timeout 180 "./${SRC%.cpp}" $MODEL "$@" 2>&1 | grep -v 'no version information'
echo "exit=$?"
