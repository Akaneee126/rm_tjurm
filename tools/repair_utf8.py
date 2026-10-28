"""Repair files whose UTF-8 multi-byte characters were truncated by a
PowerShell (cp936) round-trip: every damaged character lost its final
continuation byte, which became '?'.

Strategy: walk the byte stream; wherever a multi-byte sequence is invalid,
emit a single '?' for the whole broken character and skip the stray
continuation bytes + the '?' that replaced the lost byte.

This only ever touches bytes that are already invalid UTF-8, so genuine
'?' characters in the text are untouched.
"""
import sys

def repair(path):
    b = open(path, 'rb').read()
    n = len(b)
    out = bytearray()
    i = 0
    fixed = 0
    while i < n:
        c = b[i]
        if c < 0x80:
            out.append(c)
            i += 1
            continue
        if 0xC2 <= c <= 0xDF:
            ln = 2
        elif 0xE0 <= c <= 0xEF:
            ln = 3
        elif 0xF0 <= c <= 0xF4:
            ln = 4
        else:
            out.append(0x3F)
            i += 1
            fixed += 1
            continue

        chunk = b[i:i + ln]
        if len(chunk) == ln and all(0x80 <= x <= 0xBF for x in chunk[1:]):
            out.extend(chunk)
            i += ln
            continue

        # damaged sequence: count valid continuation bytes
        k = 1
        while k < ln and i + k < n and 0x80 <= b[i + k] <= 0xBF:
            k += 1
        out.append(0x3F)
        fixed += 1
        i = i + k + (1 if (i + k < n and b[i + k] == 0x3F) else 0)

    data = bytes(out)
    try:
        data.decode('utf-8')
    except UnicodeDecodeError as e:
        print('STILL INVALID:', path, e)
        return False

    open(path, 'wb').write(data)
    print('%-72s repaired %d char(s), bytes %d -> %d' % (path, fixed, n, len(data)))
    return True


ok = True
for p in sys.argv[1:]:
    ok = repair(p) and ok
sys.exit(0 if ok else 1)
