#!/bin/sh
set -e
cp -f /root/TJURM-main/data/uniconfig/Config.json /etc/openrm/
chmod 777 /etc/openrm/Config.json
cp -f /root/TJURM-main/build/TJURM-2024 /usr/local/bin/
chmod 777 /usr/local/bin/TJURM-2024
echo "== final smoke run (8s) =="
cd /etc/openrm
timeout 8 TJURM-2024 2>&1 | grep -v -E "no version|MESA|driver name" | head -15
echo "== exit code: $? =="
