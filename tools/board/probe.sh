#!/bin/sh
echo "== os =="
head -3 /etc/os-release
echo "== arch =="
uname -m
echo "== toolchain =="
for t in cmake g++ gcc make pkg-config ldconfig; do
    p=$(command -v "$t" 2>/dev/null)
    printf '%-12s %s\n' "$t" "${p:-MISSING}"
done
cmake --version 2>/dev/null | head -1
g++ --version 2>/dev/null | head -1
echo "== aidlite pkgs =="
dpkg -l 2>/dev/null | grep -i aidlite
echo "== aidlite files =="
ls -l /usr/local/lib/libaidlite* 2>/dev/null
ls -l /usr/local/include/aidlux/aidlite/ 2>/dev/null
echo "== apt libs =="
dpkg -l 2>/dev/null | grep -E "libopencv-dev|libeigen3-dev|libceres-dev|libncurses|nlohmann" | awk '{print $2, $3}'
echo "== opencv cmake config =="
ls -d /usr/lib/*/cmake/opencv4 2>/dev/null
echo "== nproc/mem =="
nproc
free -h | head -2
echo "== usb camera =="
ls -l /dev/video* 2>/dev/null
echo "== serial =="
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
echo "== existing dirs =="
ls -d /home/* 2>/dev/null
echo "== /etc/openrm =="
ls -l /etc/openrm 2>/dev/null
