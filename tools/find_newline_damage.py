"""List comment lines where the trailing '?' was a lost newline, i.e. the
comment now runs into the next code line."""
import io
import sys

out = io.open('tools/newline_report.txt', 'w', encoding='utf-8')
for p in sys.argv[1:]:
    lines = io.open(p, encoding='utf-8').read().split('\n')
    out.write('== %s\n' % p)
    for n, line in enumerate(lines, 1):
        s = line.lstrip()
        if not s.startswith('//'):
            continue
        q = line.find('?')
        if q < 0:
            continue
        rest = line[q + 1:].strip()
        if not rest:
            continue
        out.write('  %5d | %s\n' % (n, line))
    out.write('\n')
out.close()
print('ok')
