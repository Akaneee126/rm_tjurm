#!/bin/sh
cd /etc/openrm
# 提高存图帧率，用来估算流水线真实吞吐
sed -i 's/"FPS" : 5/"FPS" : 120/' Config.json
rm -f debug/*.jpg
echo "== run 6s with imwrite @120 =="
timeout 6 TJURM-2024 >/tmp/run1.log 2>&1
N=$(ls debug/*.jpg 2>/dev/null | wc -l)
echo "images written: $N  -> pipeline approx $(echo "scale=1; $N/6" | bc) fps"
echo "== keep 2 samples =="
ls debug/*.jpg | head -2
ls debug/*.jpg | tail -1
cp "$(ls debug/*.jpg | head -1)" /root/smoke/pipe_a.jpg 2>/dev/null
cp "$(ls debug/*.jpg | tail -1)" /root/smoke/pipe_b.jpg 2>/dev/null
ls -l /root/smoke/pipe_*.jpg 2>/dev/null
