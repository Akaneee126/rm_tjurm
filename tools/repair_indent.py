# -*- coding: utf-8 -*-
"""Third-pass repair: merged code lines lost their leading indentation.
Re-indent code lines that sit at column 0 while inside a brace block,
using the following line's indentation as the anchor."""
import io
import sys


def fix(path, dry=True):
    lines = io.open(path, encoding='utf-8').read().split('\n')
    depth = 0
    changed = []
    for i, line in enumerate(lines):
        stripped = line.strip()
        starts_code = bool(stripped) and not stripped.startswith(('//', '/*', '*', '#', '}'))
        if depth > 0 and starts_code and not line.startswith((' ', '\t')):
            # find next non-empty line for an indentation hint
            nxt = ''
            for j in range(i + 1, min(i + 6, len(lines))):
                if lines[j].strip():
                    nxt = lines[j]
                    break
            hint = len(nxt) - len(nxt.lstrip(' '))
            target = hint - 4 if stripped.endswith('{') else hint
            if target < 0:
                target = 0
            lines[i] = ' ' * target + line
            changed.append((i + 1, lines[i]))
        # update depth with this line (rough: ignores braces in strings)
        if not stripped.startswith('//'):
            depth += line.count('{') - line.count('}')
    print('== %s  (%d line(s))' % (path, len(changed)))
    for ln, text in changed:
        print('   %5d | %s' % (ln, text))
    if not dry:
        io.open(path, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
    return len(changed)


dry = '--write' not in sys.argv
for p in [a for a in sys.argv[1:] if not a.startswith('--')]:
    fix(p, dry)
print('dry-run' if dry else 'written')
