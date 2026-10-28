import sys, io

for p in sys.argv[1:]:
    b = open(p, 'rb').read()
    res = []
    for enc in ('utf-8', 'gbk', 'cp936'):
        try:
            b.decode(enc)
            res.append(enc + ':OK')
        except UnicodeDecodeError as e:
            res.append(enc + ':FAIL@%d' % e.start)
    # count non-ascii bytes
    na = sum(1 for x in b if x > 127)
    print('%-70s nonascii=%5d  %s' % (p, na, ' '.join(res)))
