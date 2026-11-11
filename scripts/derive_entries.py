"""Derive the next round of missing entry points and append them to the manifest.

usage: python -I derive_entries.py IMAGE GENERATED_DIR CODEGEN_LOG MANIFEST

Sources, all checked against the functions codegen already emitted:
  * orphan stubs: `b` targets codegen reported as "not in any function",
    sized up to the next known function;
  * data words and lis/addi pairs that point at code after a block terminator
    (find_ptr_targets.py), minus relative jump-table bases (target right after a
    bctr whose address is built less than 0x80 bytes earlier);
  * code gaps codegen skipped inside functions (find_hidden_entries.py).
"""
import bisect
import json
import os
import re
import subprocess
import sys
import tomllib

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = 0x82000000


def run_scan(script, image, gen, out):
    subprocess.run([sys.executable, "-I", os.path.join(HERE, script), image, gen, out], check=True)
    rows = []
    for line in open(out):
        m = re.match(r"0x([0-9A-F]+) = \{\}(?:\s+# refs: \d+, first at 0x([0-9A-F]+))?", line)
        if m:
            rows.append((int(m.group(1), 16), int(m.group(2), 16) if m.group(2) else None))
    return rows


def main():
    image, gen, log, manifest = sys.argv[1:5]
    img = open(image, "rb").read()
    word = lambda a: int.from_bytes(img[a - BASE:a - BASE + 4], "big")
    starts = sorted(int(k, 16) for k in json.load(open(os.path.join(gen, "codegen.partition.json")))["assignments"])
    have = {int(k, 16) for k in tomllib.load(open(manifest, "rb"))["entrypoint"].get("functions", {})}

    new = {}
    # Orphan tail stubs.
    for m in re.finditer(r"b 0x([0-9A-F]{8}) from 0x[0-9A-F]{8} - target not in any function", open(log, errors="replace").read()):
        t = int(m.group(1), 16)
        nxt = starts[bisect.bisect_right(starts, t)]
        new[t] = f"{{ size = 0x{nxt - t:X} }}  # orphan tail stub"
    # Pointer targets (data + code-built).
    for t, ref in run_scan("find_ptr_targets.py", image, gen, os.path.join(os.path.dirname(log), "derive_ptr.toml")):
        if word(t - 4) == 0x4E800420 and ref is not None and 0 < t - ref < 0x80:
            continue  # relative jump-table base
        new.setdefault(t, "{}  # pointer target")
    # Skipped code gaps.
    for t, _ in run_scan("find_hidden_entries.py", image, gen, os.path.join(os.path.dirname(log), "derive_gaps.toml")):
        if word(t) >> 26 != 0:  # opcode 0 is never code (padding/data)
            new.setdefault(t, "{}  # skipped gap after terminator")

    fresh = {t: v for t, v in sorted(new.items()) if t not in have}
    # Sized entries must end before the next declared entry (the loader rejects overlaps).
    declared = sorted(have | set(fresh))
    for t, v in fresh.items():
        m = re.match(r"\{ size = 0x([0-9A-F]+) \}", v)
        if m:
            nxt = declared[bisect.bisect_right(declared, t)] if bisect.bisect_right(declared, t) < len(declared) else None
            if nxt is not None and t + int(m.group(1), 16) > nxt:
                fresh[t] = v.replace(m.group(0), f"{{ size = 0x{nxt - t:X} }}")
    if fresh:
        with open(manifest, "a") as f:
            for t, v in fresh.items():
                f.write(f"0x{t:08X} = {v}\n")
    subprocess.run([sys.executable, "-I", os.path.join(HERE, "normalize_manifest.py"), manifest], check=True)
    print(f"{len(fresh)} new entries appended ({len(new)} derived, {len(have)} already present)")


if __name__ == "__main__":
    main()
