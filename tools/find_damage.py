import sys, io

def damage_sites(b):
    sites = []
    i, n = 0, len(b)
    while i < n:
        c = b[i]
        if c < 0x80:
            i += 1
            continue
        if 0xC2 <= c <= 0xDF:
            ln = 2
        elif 0xE0 <= c <= 0xEF:
            ln = 3
        elif 0xF0 <= c <= 0xF4:
            ln = 4
        else:
            sites.append((i, c))
            i += 1
            continue
        chunk = b[i:i + ln]
        if len(chunk) == ln and all(0x80 <= x <= 0xBF for x in chunk[1:]):
            i += ln
        else:
            sites.append((i, c))
            i += 1
    return sites

out = io.open('tools/damage_report.txt', 'w', encoding='utf-8')
for p in sys.argv[1:]:
    b = open(p, 'rb').read()
    sites = damage_sites(b)
    out.write('== %s  sites=%d  bytes=%d\n' % (p, len(sites), len(b)))
    for s, c in sites:
        lo = max(0, s - 40)
        hi = min(len(b), s + 12)
        ctx = b[lo:hi].decode('utf-8', 'replace').replace('\n', '\\n')
        out.write('   @%-6d %s\n' % (s, ctx))
    out.write('\n')
out.close()
print('written tools/damage_report.txt')
