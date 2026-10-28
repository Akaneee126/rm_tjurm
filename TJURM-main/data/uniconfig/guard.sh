#!/bin/bash

sleep 5

# 仅 Nvidia Jetson 平台存在这两个命令，AidLux 犀牛派上直接跳过
if command -v nvpmodel >/dev/null 2>&1; then
    nvpmodel -m 2
fi
if command -v jetson_clocks >/dev/null 2>&1; then
    jetson_clocks
fi

while true; do
    pkill TJURM-2024
    TJURM-2024
    sleep 1
done
