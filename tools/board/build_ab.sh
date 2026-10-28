#!/bin/sh
set -e
cd /root/smoke
export PKG_CONFIG_PATH=/usr/lib/aarch64-linux-gnu/pkgconfig
CV=$(pkg-config --libs opencv4)
g++ -O2 -std=c++17 ab_flip.cpp \
    -I/usr/local/include -I/usr/local/include/openrm -I/usr/include/eigen3 -I/usr/include/opencv4 \
    -L/usr/local/lib -Wl,--no-as-needed -lopenrm_infer -lopenrm_uniterm -lopenrm_timer -Wl,--as-needed -lopenrm_video -laidlite \
    $CV -Wl,--allow-shlib-undefined -o ab_flip
echo "== run (production path: rm::Infer + NMS) =="
LD_LIBRARY_PATH=/usr/local/lib ./ab_flip /etc/openrm/models/0526_qcs6490_w8a16.qnn231.ctx.bin.aidem 8 2>&1 | grep -v "no version"
