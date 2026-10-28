#!/bin/sh
echo "== lsusb =="
lsusb 2>/dev/null
echo
echo "== video nodes =="
for d in /sys/class/video4linux/*; do
    [ -e "$d" ] || continue
    n=$(basename "$d")
    echo "$n : $(cat $d/name 2>/dev/null)"
done
echo
echo "== by-id =="
ls -l /dev/v4l/by-id/ 2>/dev/null
ls -l /dev/v4l/by-path/ 2>/dev/null
echo
echo "== formats of capture nodes =="
for i in 0 1 2 4 8 32 33 64 72; do
    [ -c /dev/video$i ] || continue
    fmt=$(v4l2-ctl -d /dev/video$i --list-formats 2>/dev/null | grep -o "\[[0-9]\]:.*" | head -4 | tr '\n' ' ')
    caps=$(v4l2-ctl -d /dev/video$i --info 2>/dev/null | grep -E "Card type|Driver name" | tr '\n' ' ')
    [ -n "$caps" ] && echo "video$i: $caps | $fmt"
done
echo
echo "== hikvision sdk? =="
ls -l /opt/MVS /usr/lib/libMvCameraControl* /usr/local/lib/libMvCameraControl* 2>/dev/null || echo "no MVS SDK"
