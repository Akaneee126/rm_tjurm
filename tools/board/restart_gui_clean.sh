#!/bin/sh
# 关掉存图，重启界面（保持 FlipMode 生效）
python3 - <<'PY'
import json
p = '/etc/openrm/Config.json'
d = json.load(open(p))
d['Debug']['ImageThread']['Imwrite'] = False
json.dump(d, open(p, 'w'), indent=4, ensure_ascii=False)
print('Imwrite =', d['Debug']['ImageThread']['Imwrite'], '| FlipMode =', d['Camera']['Base'].get('FlipMode'))
PY
rm -f /etc/openrm/debug/*.jpg
pkill TJURM-2024 2>/dev/null || true
sleep 1
if ! pgrep -f "Xtigervnc :1" >/dev/null; then
    setsid Xtigervnc :1 -geometry 1440x1080 -depth 24 -rfbport 5901 \
        -AlwaysShared -PasswordFile /root/.vnc/passwd < /dev/null > /tmp/vnc.log 2>&1 &
    sleep 2
fi
cd /etc/openrm
DISPLAY=:1 setsid TJURM-2024 -s < /dev/null > /tmp/tjurm_gui.log 2>&1 &
sleep 5
echo "== 运行中 =="
pgrep -af "TJURM-2024|Xtigervnc" | head -2
DISPLAY=:1 xwininfo -root -tree 2>/dev/null | grep -i tjurm | head -1
grep -v -E "no version" /tmp/tjurm_gui.log | tail -2
