#!/bin/sh
echo "== before =="
lsusb | grep -i hik
set -- $(lsusb | grep -i "2bdf:0001" | sed -e 's/Bus \([0-9]*\) Device \([0-9]*\).*/\1 \2/')
BUS=$(printf "%03d" "$1")
DEV=$(printf "%03d" "$2")
echo "bus=$BUS dev=$DEV"
/root/smoke/usbreset /dev/bus/usb/$BUS/$DEV
sleep 3
echo "== after =="
lsusb | grep -i hik
cat /sys/bus/usb/devices/*/idVendor 2>/dev/null | grep -c 2bdf
echo "== enumerate again =="
LD_LIBRARY_PATH=/usr/local/lib timeout 30 /root/smoke/hik_probe 2>&1 | head -12
