import io
import json
import collections

p = r'TJURM-main\data\uniconfig\Config.json'
d = json.load(open(p, encoding='utf-8'), object_pairs_hook=collections.OrderedDict)
cam = d['Camera']
for side in ('Base', 'Far'):
    if side in cam:
        cam[side]['FlipMode'] = 3   # 3 = Y轴 + X轴 = 180° 旋转
json.dump(d, open(p, 'w', encoding='utf-8'), indent=4, ensure_ascii=False)
print('FlipMode =', cam['Base']['FlipMode'], '(3 = Y+X 180°旋转)')
