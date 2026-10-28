#!/bin/sh
# 用生产路径在"已翻转 + 正对装甲板"的实时画面上验证检测
pkill TJURM-2024 2>/dev/null || true
sleep 1
cd /root/smoke
rm -f live_det_*.jpg
LD_LIBRARY_PATH=/usr/local/lib ./test11 /etc/openrm/models/0526_qcs6490_w8a16.qnn231.ctx.bin.aidem 6 2>&1 \
    | grep -v -E "no version|license|rpcmem" | head -20
cp -f live_det_0.jpg /root/smoke/flip_det.jpg 2>/dev/null || true
ls -l /root/smoke/flip_det.jpg 2>/dev/null
