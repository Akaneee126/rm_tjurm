#!/bin/sh
# 重启（带 Y 轴翻转）并落一张图验证翻转是否生效
python3 - <<'PY'
import json
p = '/etc/openrm/Config.json'
d = json.load(open(p))
d['Debug']['ImageThread']['Imwrite'] = True
d['Debug']['ImageThread']['FPS'] = 10
json.dump(d, open(p, 'w'), indent=4, ensure_ascii=False)
print('FlipMode =', d['Camera']['Base'].get('FlipMode'))
PY
rm -f /etc/openrm/debug/*.jpg
pkill TJURM-2024 2>/dev/null || true
if ! pgrep -f "Xtigervnc :1" >/dev/null; then
    setsid Xtigervnc :1 -geometry 1440x1080 -depth 24 -rfbport 5901 \
        -AlwaysShared -PasswordFile /root/.vnc/passwd < /dev/null > /tmp/vnc.log 2>&1 &
    sleep 2
fi
cd /etc/openrm
DISPLAY=:1 setsid TJURM-2024 -s < /dev/null > /tmp/tjurm_flip.log 2>&1 &
sleep 7
echo "images: $(ls /etc/openrm/debug/*.jpg 2>/dev/null | wc -l)"
ls /etc/openrm/debug/*.jpg 2>/dev/null | tail -1 | while read f; do cp "$f" /root/smoke/flip_check.jpg; done
echo "window:"; DISPLAY=:1 xwininfo -root -tree 2>/dev/null | grep -i tjurm | head -1
echo "log:"; grep -v -E "no version" /tmp/tjurm_flip.log | tail -3
