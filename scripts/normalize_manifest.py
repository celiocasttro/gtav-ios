"""Clamp every sized [entrypoint.functions] entry so it ends before the next declared entry.

usage: python -I normalize_manifest.py MANIFEST

The ReXGlue loader rejects overlapping boundaries; derive rounds add entries inside
earlier orphan-stub ranges, so sizes are re-clamped against the full declared set.
"""
import re
import sys
import tomllib

path = sys.argv[1]
lines = open(path).read().splitlines(True)
addrs = sorted(int(k, 16) for k in tomllib.load(open(path, "rb"))["entrypoint"].get("functions", {}))
entry = re.compile(r"^(0x([0-9A-Fa-f]+) = \{ size = 0x)([0-9A-Fa-f]+)( \}.*)$", re.S)
changed = 0
for i, line in enumerate(lines):
    m = entry.match(line)
    if not m:
        continue
    start, size = int(m.group(2), 16), int(m.group(3), 16)
    later = [a for a in addrs if a > start]
    if later and start + size > later[0]:
        lines[i] = f"{m.group(1)}{later[0] - start:X}{m.group(4)}"
        changed += 1
open(path, "w").writelines(lines)
tomllib.load(open(path, "rb"))
print(f"clamped {changed} sized entries")
