import json, collections, io, sys

p = sys.argv[1]
d = json.load(open(p, encoding='utf-8'), object_pairs_hook=collections.OrderedDict)
if 'UVC1280_1024' not in d:
    d['UVC1280_1024'] = collections.OrderedDict(d['MindVision1280_1024'])
if 'UVC720_540' not in d:
    d['UVC720_540'] = collections.OrderedDict(d['DaHeng720_540'])
with io.open(p, 'w', encoding='utf-8') as f:
    json.dump(d, f, indent=4, ensure_ascii=False)
    f.write('\n')
print('ok', list(d.keys()))
