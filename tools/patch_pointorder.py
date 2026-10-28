import io
import json
import collections

# 1) detectors: read PointOrder from config
ANCHOR = '    if (apply_sigmoid) rm::message("YOLO output treated as logits (sigmoid applied)", rm::MSG_NOTE);'
ADD = (ANCHOR + '\n\n'
       '    // 四点顺序模式（0=旧模型默认，3=模型直接输出 左上-左下-右下-右上）\n'
       '    rm::yoloSetPointOrder(ModelConfig::intOf(model_cfg, "PointOrder", 0));')

for p in [r'TJURM-main\TJURM-main\src\threads\pipeline\baseline\detector_thread.cpp',
          r'TJURM-main\TJURM-main\src\threads\pipeline\fourpoints\detector_thread.cpp']:
    t = io.open(p, encoding='utf-8').read()
    if 'yoloSetPointOrder' in t:
        print('already patched:', p)
        continue
    if ANCHOR not in t:
        print('ANCHOR MISSING in', p)
        continue
    t = t.replace(ANCHOR, ADD, 1)
    io.open(p, 'w', encoding='utf-8').write(t)
    print('patched:', p)

# 2) config: PointOrder = 3 for FPX
p = r'TJURM-main\TJURM-main\data\uniconfig\Config.json'
d = json.load(open(p, encoding='utf-8'), object_pairs_hook=collections.OrderedDict)
fpx = d['Model']['YoloArmor']['FPX']
fpx['PointOrder'] = 3
json.dump(d, open(p, 'w', encoding='utf-8'), indent=4, ensure_ascii=False)
print('PointOrder =', fpx['PointOrder'], '| ApplySigmoid =', fpx['ApplySigmoid'], '| DirModel =', fpx['DirModel'])
