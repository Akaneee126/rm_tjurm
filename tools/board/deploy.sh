#!/bin/sh
set -e
echo "== 1. /etc/openrm =="
mkdir -p /etc/openrm/models /etc/openrm/image /etc/openrm/video /etc/openrm/debug
cp -f /root/TJURM-main/data/uniconfig/Config.json /etc/openrm/
cp -f /root/TJURM-main/data/uniconfig/CamLens.json /etc/openrm/
cp -f /root/TJURM-main/data/uniconfig/guard.sh /etc/openrm/ 2>/dev/null || true
cp -f /root/TJURM-main/data/uniconfig/image/*.png /etc/openrm/image/ 2>/dev/null || true
chmod -R 777 /etc/openrm

echo "== 2. model =="
cp -f /root/models/0526_qcs6490_w8a8.qnn231.ctx.bin.aidem /etc/openrm/models/
ls -l /etc/openrm/models/

echo "== 3. run config (video replay, no camera attached) =="
python3 - <<'PY'
import json
p = '/etc/openrm/Config.json'
d = json.load(open(p))
d['Camera']['Driver'] = 'Video'
d['Camera']['Base']['VideoPath'] = '/root/smoke/scene.avi'
d['Camera']['Base']['Loop'] = True
d['Camera']['Base']['FrameRate'] = 30.0
d['Debug']['Control']['Serial'] = False
d['Debug']['Control']['Timeout'] = False
json.dump(d, open(p, 'w'), indent=4, ensure_ascii=False)
print('driver =', d['Camera']['Driver'], 'video =', d['Camera']['Base']['VideoPath'])
print('model  =', d['Model']['YoloArmor'][d['Model']['YoloArmor']['Type']]['DirModel'])
PY

echo "== 4. copy binary =="
cp -f /root/TJURM-main/build/TJURM-2024 /usr/local/bin/
chmod 777 /usr/local/bin/TJURM-2024
echo done
