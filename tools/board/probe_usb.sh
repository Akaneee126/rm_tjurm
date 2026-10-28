#!/bin/sh
echo "=== env vars the SDK reads ==="
strings /usr/local/lib/libMvCameraControl.so | grep -E "^MVCAM|MVCAM_|RUNENV|^MVS" | sort -u | head -20
echo
echo "=== camera sysfs ==="
for d in /sys/bus/usb/devices/*; do
    v=$(cat "$d/idVendor" 2>/dev/null)
    if [ "$v" = "2bdf" ]; then
        echo "path: $d"
        echo "  idProduct=$(cat $d/idProduct) speed=$(cat $d/speed)"
        echo "  bNumInterfaces=$(cat $d/bNumInterfaces 2>/dev/null)"
        echo "  bConfigurationValue=$(cat $d/bConfigurationValue 2>/dev/null)"
        echo "  driver=$(basename $(readlink $d/driver 2>/dev/null) 2>/dev/null)"
        ls "$d" | grep -E "^[0-9]" | while read i; do
            echo "  interface $i driver=$(basename $(readlink $d/$i/driver 2>/dev/null) 2>/dev/null)"
        done
    fi
done
echo
echo "=== lsusb -v (interfaces) ==="
lsusb -v -d 2bdf:0001 2>/dev/null | grep -E "bInterfaceClass|bInterfaceSubClass|bInterfaceProtocol|bcdUSB|iProduct|bNumEndpoints" | head -20
echo
echo "=== dmesg usb ==="
dmesg 2>/dev/null | grep -iE "usb 2-|new .* device|2bdf" | tail -10
