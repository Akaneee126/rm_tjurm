#!/bin/sh
set -e
python3 - <<'PY'
import json
p = '/etc/openrm/Config.json'
d = json.load(open(p))
d['Camera']['Driver'] = 'Video'
d['Camera']['Base']['VideoPath'] = '/root/smoke/demo.avi'
d['Camera']['Base']['Loop'] = True
d['Debug']['ImageThread']['Imwrite'] = True
d['Debug']['ImageThread']['FPS'] = 20
json.dump(d, open(p, 'w'), indent=4, ensure_ascii=False)
PY
rm -f /etc/openrm/debug/*.jpg
pkill TJURM-2024 2>/dev/null || true
sleep 1
cd /etc/openrm
DISPLAY=:1 nohup TJURM-2024 -s > /tmp/tjurm_gui2.log 2>&1 &
sleep 8
echo "images: $(ls /etc/openrm/debug/*.jpg 2>/dev/null | wc -l)"
ls /etc/openrm/debug/*.jpg 2>/dev/null | tail -1 | while read f; do cp "$f" /root/smoke/gui_box.jpg; done
ls -l /root/smoke/gui_box.jpg 2>/dev/null
