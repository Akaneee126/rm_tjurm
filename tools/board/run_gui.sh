#!/bin/sh
# 起虚拟显示 + 带 imshow 跑自瞄，并落一张带框的图供核对
set -e
DISP=:1

# 1) 虚拟显示
if ! pgrep -f "Xtigervnc ${DISP}" >/dev/null; then
    Xtigervnc ${DISP} -geometry 1440x1080 -depth 24 \
        -rfbport 5901 -AlwaysShared -AcceptKeyEvents -AcceptPointerEvents \
        -PasswordFile /root/.vnc/passwd > /tmp/vnc.log 2>&1 &
    sleep 2
fi
echo "== Xvnc =="
ss -ltn 2>/dev/null | grep 5901 || echo "no 5901"

# 2) 打开存图（顺便验证画框）
python3 - <<'PY'
import json
p = '/etc/openrm/Config.json'
d = json.load(open(p))
d['Debug']['ImageThread']['Imwrite'] = True
d['Debug']['ImageThread']['FPS'] = 10
json.dump(d, open(p, 'w'), indent=4, ensure_ascii=False)
PY
rm -f /etc/openrm/debug/*.jpg

# 3) 跑
pkill TJURM-2024 2>/dev/null || true
sleep 1
cd /etc/openrm
DISPLAY=${DISP} nohup TJURM-2024 -s > /tmp/tjurm_vnc.log 2>&1 &
sleep 8

echo "== 进程 =="
ps -ef | grep -v grep | grep TJURM-2024 | head -2
echo "== 日志 =="
grep -v -E "no version" /tmp/tjurm_vnc.log | tail -6
echo "== 窗口 =="
DISPLAY=${DISP} xwininfo -root -tree 2>/dev/null | grep -i tjurm | head -2
echo "== 落图 =="
ls /etc/openrm/debug/*.jpg 2>/dev/null | wc -l
ls /etc/openrm/debug/*.jpg 2>/dev/null | tail -1 | while read f; do cp "$f" /root/smoke/gui_frame.jpg; done
ls -l /root/smoke/gui_frame.jpg 2>/dev/null
