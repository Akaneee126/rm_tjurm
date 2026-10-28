#!/bin/sh
# 编译并运行海康相机自检（不需要 AidLite）
set -e
cd /root/smoke
g++ -O2 -std=c++17 hik_probe.cpp \
    -I/root/OpenRM-main/include/video/hikvision \
    -L/usr/local/lib -lMvCameraControl -o hik_probe
echo "== run =="
LD_LIBRARY_PATH=/usr/local/lib timeout 60 ./hik_probe 2>&1
echo "exit=$?"
