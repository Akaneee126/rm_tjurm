#!/bin/sh
# 鍦ㄦ澘瀛愪笂璧蜂竴涓櫄鎷熸樉绀?+ 璺戝甫 imshow 鐨勮嚜鐬勶紙渚?VNC 瑙傜湅锛?set -e
DISP="${VDISP:-:1}"

# 1) 璁句竴涓凡鐭ョ殑 VNC 瀵嗙爜锛堝師鏉ユ槸闀滃儚鑷甫銆佸瘑鐮佹湭鐭ワ級
printf 'rm2024\n' | vncpasswd -f > /root/.vnc/passwd
chmod 600 /root/.vnc/passwd

# 2) 璧疯櫄鎷熸樉绀猴紙涓嶈蛋 gnome-session锛岀渷璧勬簮锛?pkill -f "Xtigervnc ${DISP}" 2>/dev/null || true
sleep 1
Xtigervnc ${DISP} -geometry 1440x1080 -depth 24 \
    -rfbport 590${DISP#:} -AlwaysShared -AcceptKeyEvents -AcceptPointerEvents \
    -PasswordFile /root/.vnc/passwd > /tmp/vnc.log 2>&1 &
sleep 2

echo "== Xvnc 鐘舵€?=="
ps -ef | grep -v grep | grep Xtigervnc | head -2
ss -ltnp 2>/dev/null | grep 590 || true

echo "== 妫€鏌?DISPLAY =="
DISPLAY=${DISP} xdpyinfo 2>&1 | head -3 || echo "xdpyinfo missing"

echo "== 鍚姩 TJURM -s锛堝悗鍙帮紝10 绉掑悗鑷姩鍋滐級=="
pkill TJURM-2024 2>/dev/null || true
sleep 1
cd /etc/openrm
DISPLAY=${DISP} nohup TJURM-2024 -s > /tmp/tjurm_vnc.log 2>&1 &
sleep 6
echo "-- 杩涚▼ --"
ps -ef | grep -v grep | grep TJURM-2024 | head -2
echo "-- 鏃ュ織 --"
grep -v -E "no version" /tmp/tjurm_vnc.log | tail -8
echo "-- 绐楀彛鍒楄〃 --"
DISPLAY=${DISP} xwininfo -root -tree 2>/dev/null | grep -E "tjurm|\"" | head -6 || true
