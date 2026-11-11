"""Find entry points the analyzer swallowed into a preceding function.

usage: python -I find_hidden_entries.py BASEFILE GENERATED_DIR OUT_TOML

Inside each generated function, an instruction that directly follows an unconditional
terminator (b, blr, bctr) with no `loc_` label in between is unreachable from inside the
function, including from its switch tables (every branch/case target gets a label).
Code there is a separate entry point reached only through a computed pointer.
"""
import glob
import os
import re
import struct
import sys

BASE = 0x82000000
HEAD = re.compile(r"^DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\)")
LABEL = re.compile(r"^loc_([0-9A-F]{8}):")


def is_terminator(insn):
    op = insn >> 26
    return insn in (0x4E800020, 0x4E800420) or (op == 18 and not insn & 1)


def main():
    img = open(sys.argv[1], "rb").read()
    word = lambda a: struct.unpack_from(">I", img, a - BASE)[0]
    global lo_c, hi_c
    pe = struct.unpack_from("<I", img, 0x3C)[0]
    nsec, opt = struct.unpack_from("<H", img, pe + 6)[0], struct.unpack_from("<H", img, pe + 20)[0]
    lo_c, hi_c = 0xFFFFFFFF, 0
    for i in range(nsec):  # bounds of the executable sections
        o = pe + 24 + opt + i * 40
        vsize, va = struct.unpack_from("<II", img, o + 8)
        if struct.unpack_from("<I", img, o + 36)[0] & 0x20000000:
            lo_c, hi_c = min(lo_c, BASE + va), max(hi_c, BASE + va + vsize)
    found = []
    ends = []  # (start, end, last instruction word) per emitted function
    for path in glob.glob(os.path.join(sys.argv[2], "*_recomp.*.cpp")):
        start = None
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                m = HEAD.match(line)
                if m:
                    start = addr = int(m.group(1), 16)
                    prev = None  # previous emitted instruction word, None right after a label
                    continue
                if start is None:
                    continue
                m = LABEL.match(line)
                if m:
                    target = int(m.group(1), 16)
                    # Codegen skips unreachable code and inline data, so a label past the
                    # current address marks a gap. Code in a gap that follows a terminator
                    # and is not a jump table is a separate entry point.
                    if prev is not None and target > addr and is_terminator(prev):
                        gap = [word(a) for a in range(addr, target, 4)]
                        table = all(lo_c <= w < hi_c or w < 0x10000 for w in gap)
                        if not table and gap[0] != 0:
                            found.append((addr, start))
                    addr = target
                    prev = None
                elif line.startswith("	// ") and "FATAL" not in line:
                    prev = word(addr)
                    addr += 4
                elif line.startswith("}"):
                    ends.append((start, addr, prev))
                    start = None
    # Gaps between functions: code after one function's final terminator that no
    # function covers (codegen ends the function at the bctr/blr/b and moves on).
    ends.sort()
    for i, (fstart, fend, last) in enumerate(ends[:-1]):
        nxt = ends[i + 1][0]
        if last is None or not is_terminator(last) or nxt <= fend or nxt - fend > 0x10000:
            continue
        gap = [word(a) for a in range(fend, nxt, 4)]
        if gap[0] == 0 or all(lo_c <= w < hi_c or w < 0x10000 for w in gap):
            continue
        found.append((fend, fstart))
    found.sort()
    with open(sys.argv[3], "w") as out:
        out.write("# Unlabelled code after an unconditional terminator (find_hidden_entries.py).\n")
        for a, parent in found:
            out.write(f"0x{a:08X} = {{}}  # inside sub_{parent:08X}\n")
    print(f"{len(found)} hidden entries -> {sys.argv[3]}")


if __name__ == "__main__":
    main()
