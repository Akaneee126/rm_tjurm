#!/bin/sh
echo "== 用户 / sudo =="
id -u; command -v sudo >/dev/null && echo "sudo OK" || echo "no sudo"
echo
echo "== 显示环境 =="
echo "DISPLAY=${DISPLAY:-<empty>}"
echo "WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-<empty>}"
echo "XDG_SESSION_TYPE=${XDG_SESSION_TYPE:-<empty>}"
ls -l /tmp/.X11-unix 2>/dev/null || echo "no /tmp/.X11-unix"
ps -ef 2>/dev/null | grep -E "Xorg|Xwayland|weston|gnome-shell|lightsdm|gdm" | grep -v grep | head -5
echo
echo "== 图形相关包 =="
dpkg -l 2>/dev/null | grep -E "^ii  (xserver-xorg|xorg|xvfb|tigervnc|x11vnc|libgtk-3|libqt5gui)" | awk '{print $2, $3}' | head -12
echo
echo "== OpenCV highgui 后端 =="
ldd /usr/lib/aarch64-linux-gnu/libopencv_highgui.so 2>/dev/null | grep -E "gtk|qt|wayland|X11|xcb" | head -8
echo
echo "== 试跑 -s（5 秒）=="
cd /etc/openrm
timeout 5 TJURM-2024 -s 2>&1 | head -15
echo "== exit=$? =="
