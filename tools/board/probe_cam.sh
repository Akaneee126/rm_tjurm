#!/bin/sh
echo "== video devices =="
for d in /sys/class/video4linux/*; do
    [ -e "$d" ] || continue
    n=$(basename "$d")
    echo "$n : $(cat $d/name 2>/dev/null)  index=$(cat $d/index 2>/dev/null)"
    echo "    caps: $(v4l2-ctl -d /dev/$n --all 2>/dev/null | grep -E 'Device Caps|Video Capture|Streaming' | tr '\n' '|')"
done
echo "== which are UVC (usb) =="
ls -l /dev/v4l/by-id/ 2>/dev/null
echo "== who has the camera =="
fuser -v /dev/video0 /dev/video1 /dev/video32 /dev/video33 2>&1 | head
echo "== try open each =="
for i in 0 1 32 33; do
    timeout 3 dd if=/dev/video$i of=/dev/null bs=1 count=1 2>/dev/null && echo "video$i readable" || echo "video$i open/read failed"
done
