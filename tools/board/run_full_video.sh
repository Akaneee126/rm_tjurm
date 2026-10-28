#!/bin/sh
set -e
cp -f /root/TJURM-main/data/uniconfig/Config.json /etc/openrm/
chmod 777 /etc/openrm/Config.json
sh /root/build_tjurm.sh >/tmp/bt.log 2>&1 || { echo "BUILD FAIL"; grep -E "error:" /tmp/bt.log | head -10; exit 1; }
tail -2 /tmp/bt.log
cp -f /root/TJURM-main/build/TJURM-2024 /usr/local/bin/
chmod 777 /usr/local/bin/TJURM-2024

# 用录像回放跑整机（相机现在没进光）
python3 - <<'PY'
import json
p = '/etc/openrm/Config.json'
d = json.load(open(p))
d['Camera']['Driver'] = 'Video'
d['Camera']['Base']['VideoPath'] = '/root/smoke/demo.avi'
d['Camera']['Base']['Loop'] = True
d['Camera']['Base']['FrameRate'] = 60.0
d['Debug']['ImageThread']['Imwrite'] = False
json.dump(d, open(p, 'w'), indent=4, ensure_ascii=False)
print('driver=Video video=', d['Camera']['Base']['VideoPath'])
PY

echo "== run 8s (video replay, real detections) =="
cd /etc/openrm
timeout 8 TJURM-2024 2>&1 | grep -v -E "no version|MESA|driver name" | head -20
echo "exit=$?"
