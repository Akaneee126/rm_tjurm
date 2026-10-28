#!/bin/sh
# 用 setsid 完全脱离 adb shell 会话，保证进程在 shell 退出后仍然活着
DISP=:1

pkill -f "Xtigervnc ${DISP}" 2>/dev/null || true
pkill TJURM-2024 2>/dev/null || true
sleep 1

# 1) 虚拟显示（脱离会话）
setsid Xtigervnc ${DISP} -geometry 1440x1080 -depth 24 \
    -rfbport 5901 -AlwaysShared -AcceptKeyEvents -AcceptPointerEvents \
    -PasswordFile /root/.vnc/passwd < /dev/null > /tmp/vnc.log 2>&1 &
sleep 2
echo "== Xvnc =="
pgrep -af Xtigervnc | head -2
ss -ltn 2>/dev/null | grep 5901 || echo "no 5901"

# 2) 恢复真实相机
python3 - <<'PY'
import json
p = '/etc/openrm/Config.json'
d = json.load(open(p))
c = d['Camera']
c['Driver'] = 'HikVision'
b = c['Base']
b['CameraType'] = 'HikCS016_1440_1080'
b['LensType'] = 'Prime6mm'
b['Width'] = 1440
b['Height'] = 1080
b['FrameRate'] = 60.0
b['ExposureTime'] = 8000.0
b['Gain'] = 16.0
d['Debug']['ImageThread']['Imwrite'] = False
d['Debug']['ImageThread']['FPS'] = 25
json.dump(d, open(p, 'w'), indent=4, ensure_ascii=False)
print('driver =', c['Driver'], 'exp =', b['ExposureTime'])
PY

# 3) 启动自瞄（带显示，脱离会话）
cd /etc/openrm
DISPLAY=${DISP} setsid TJURM-2024 -s < /dev/null > /tmp/tjurm_gui.log 2>&1 &
sleep 6
echo "== TJURM =="
pgrep -af TJURM-2024 | head -2
echo "== 日志 =="
grep -v -E "no version" /tmp/tjurm_gui.log | tail -5
echo "== 窗口 =="
DISPLAY=${DISP} xwininfo -root -tree 2>/dev/null | grep -i tjurm | head -2
