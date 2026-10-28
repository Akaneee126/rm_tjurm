#!/bin/sh
set -e
echo "== sync config+lens and rebuild =="
cp -f /root/TJURM-main/data/uniconfig/Config.json /etc/openrm/
cp -f /root/TJURM-main/data/uniconfig/CamLens.json /etc/openrm/
chmod -R 777 /etc/openrm
mkdir -p /etc/openrm/models
cp -f /root/models/*.aidem /etc/openrm/models/ 2>/dev/null || true
sh /root/build_tjurm.sh >/tmp/bt.log 2>&1 && tail -2 /tmp/bt.log || { echo BUILD FAIL; tail -20 /tmp/bt.log; exit 1; }
cp -f /root/TJURM-main/build/TJURM-2024 /usr/local/bin/
chmod 777 /usr/local/bin/TJURM-2024

echo "== clear debug dir =="
rm -f /etc/openrm/debug/*.jpg 2>/dev/null || true

echo "== run 8s =="
cd /etc/openrm
timeout 8 TJURM-2024 2>&1 | grep -v -E "no version|MESA|driver name|^ *$" | head -25
echo "== debug images written =="
ls /etc/openrm/debug/*.jpg 2>/dev/null | wc -l
