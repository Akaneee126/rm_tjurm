#!/bin/sh
set -e
cp -f /root/TJURM-main/data/uniconfig/Config.json /etc/openrm/
python3 - <<'PY'
import json
p = '/etc/openrm/Config.json'
d = json.load(open(p))
c = d['Camera']
c['Driver'] = 'HikVision'
b = c['Base']
b['CameraType'] = 'HikCS016_1440_1080'
b['LensType'] = 'Prime6mm'
b['Width'] = 1440
b['Height'] = 1080
b['FrameRate'] = 60.0
b['ExposureTime'] = 20000.0
b['Gain'] = 16.0
d['Debug']['ImageThread']['Imwrite'] = True
d['Debug']['ImageThread']['FPS'] = 10
d['Debug']['ImageThread']['Scale'] = 0.5
json.dump(d, open(p, 'w'), indent=4, ensure_ascii=False)
print('driver=HikVision exp=%s us gain=%s' % (b['ExposureTime'], b['Gain']))
PY
chmod 777 /etc/openrm/Config.json
rm -f /etc/openrm/debug/*.jpg
echo "== run 6s =="
cd /etc/openrm
timeout 6 TJURM-2024 2>&1 | grep -v -E "no version|MESA|driver name" | head -12
echo "images: $(ls /etc/openrm/debug/*.jpg 2>/dev/null | wc -l)"
ls /etc/openrm/debug/*.jpg 2>/dev/null | tail -1 | while read f; do cp "$f" /root/smoke/live_pipe.jpg; done
ls -l /root/smoke/live_pipe.jpg 2>/dev/null
