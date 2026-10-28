import io
import json
import collections

p = r'TJURM-main\data\uniconfig\Config.json'
d = json.load(open(p, encoding='utf-8'), object_pairs_hook=collections.OrderedDict)
cam = d['Camera']
for side in ('Base', 'Far'):
    if side in cam:
        cam[side]['FlipMode'] = 1          # 1 = 沿 Y 轴翻转（上下颠倒）
        cam[side]['Rotate'] = False        # 旧字段保持 false，避免重复翻转
json.dump(d, open(p, 'w', encoding='utf-8'), indent=4, ensure_ascii=False)
print('Base.FlipMode =', cam['Base']['FlipMode'], '| Base.Rotate =', cam['Base']['Rotate'])
print('Driver =', cam['Driver'], '| ExposureTime =', cam['Base']['ExposureTime'])
