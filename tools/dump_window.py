import sys

def show(path, pos, before=120, after=120):
    b = open(path, 'rb').read()
    s = max(0, pos - before)
    e = min(len(b), pos + after)
    print('== %s @%d (len=%d)' % (path, pos, len(b)))
    print('  hex:', b[s:e].hex(' '))
    print('  asc:', ''.join(chr(x) if 32 <= x < 127 else '.' for x in b[s:e]))
    # try gbk decode of the window
    try:
        print('  gbk:', b[s:e].decode('gbk', 'replace'))
    except Exception as ex:
        print('  gbk err', ex)
    try:
        print('  utf8:', b[s:e].decode('utf-8', 'replace'))
    except Exception as ex:
        print('  utf8 err', ex)
    print()

show(sys.argv[1], int(sys.argv[2]))
