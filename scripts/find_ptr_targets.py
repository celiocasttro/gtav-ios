"""Find code addresses referenced only through data (vtables, init/atexit tables,
callbacks) that codegen did not register as functions.

usage: python -I find_ptr_targets.py BASEFILE GENERATED_DIR OUT_TOML

Function extents come from the generated C++ (one `// insn` comment per guest
instruction). A data word counts as a candidate when it points into the code
section, is 4-aligned, falls in a gap that no function covers, and the word
before it ends a block (blr/b/bctr/padding), so it is a plausible entry point.
"""
import glob
import os
import re
import struct
import sys

BASE = 0x82000000


def sections(img):
    pe = struct.unpack_from("<I", img, 0x3C)[0]
    nsec = struct.unpack_from("<H", img, pe + 6)[0]
    opt = struct.unpack_from("<H", img, pe + 20)[0]
    out = []
    for i in range(nsec):
        o = pe + 24 + opt + i * 40
        name = img[o:o + 8].rstrip(b"\0").decode("latin-1")
        vsize, va, rawsize, rawptr, _, _, _, _, flags = struct.unpack_from("<IIIIIIHHI", img, o + 8)
        out.append((name, BASE + va, vsize, rawptr, flags))
    return out


def extents(gen_dir):
    starts = {}
    head = re.compile(r"^DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\)")
    for path in glob.glob(os.path.join(gen_dir, "*_recomp.*.cpp")):
        cur = None
        n = 0
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                m = head.match(line)
                if m:
                    cur, n = int(m.group(1), 16), 0
                elif cur is not None:
                    if line.startswith("}"):
                        starts[cur] = max(n, 1) * 4
                        cur = None
                    elif line.startswith("\t// ") and "FATAL" not in line:
                        n += 1
    return starts


def main():
    img = open(sys.argv[1], "rb").read()
    secs = sections(img)
    code = [s for s in secs if s[4] & 0x20000000]  # IMAGE_SCN_MEM_EXECUTE
    data = [s for s in secs if not s[4] & 0x20000000 and s[0] not in (".pdata", ".reloc", ".idata")]
    lo = lo_c = min(s[1] for s in code)
    hi = hi_c = max(s[1] + s[2] for s in code)
    funcs = extents(sys.argv[2])
    starts = sorted(funcs)
    print(f"code 0x{lo:08X}-0x{hi:08X}, {len(funcs)} functions; data sections: {[s[0] for s in data]}")

    covered = bytearray((hi - lo) // 4)
    for a, size in funcs.items():
        for w in range((a - lo) // 4, min((a + size - lo) // 4, len(covered))):
            covered[w] = 1

    def word(addr):
        return struct.unpack_from(">I", img, addr - BASE)[0]

    def block_end(insn):
        op = insn >> 26
        return (insn == 0x4E800020 or insn == 0x4E800420 or insn == 0 or
                (op == 18 and not insn & 1))  # blr, bctr, padding, b (no link)

    cands = {}
    for name, va, vsize, _, _ in data:
        end = min(vsize, len(img) - (va - BASE))  # bss tail is not in the basefile
        for off in range(0, end - 3, 4):
            v = struct.unpack_from(">I", img, va - BASE + off)[0]
            if v & 3 or not lo <= v < hi or v in funcs:
                continue
            if covered[(v - lo) // 4]:
                continue
            if v > lo and not block_end(word(v - 4)):
                continue
            if word(v + 8) == 0x7D6903A6 and word(v + 12) == 0x4E800420:
                continue  # import thunk (2 import-record words; mtctr r11; bctr), codegen treats it as an import
            cands.setdefault(v, []).append(va + off)

    # Addresses built in code: lis rX,hi followed (within 8 insns) by addi/ori rY,rX,lo.
    # These may land inside a function the analyzer over-extended, so the coverage test is
    # replaced by "previous instruction ends a block" (thunks after a b/blr).
    code_refs = 0
    for name, va, vsize, _, flags in code:
        end = min(vsize, len(img) - (va - BASE))
        for off in range(0, end - 3, 4):
            insn = struct.unpack_from(">I", img, va - BASE + off)[0]
            if insn >> 26 != 15 or (insn >> 16) & 31:  # lis = addis rD,0,imm
                continue
            rd = (insn >> 21) & 31
            hi = (insn & 0xFFFF) << 16
            for k in range(1, 9):
                if off + 4 * k + 4 > end:
                    break
                nxt = struct.unpack_from(">I", img, va - BASE + off + 4 * k)[0]
                op, ra = nxt >> 26, (nxt >> 16) & 31
                if op in (14, 24) and ra == rd:  # addi / ori
                    lo = nxt & 0xFFFF
                    v = (hi + (lo - 0x10000 if op == 14 and lo & 0x8000 else lo)) & 0xFFFFFFFF
                    if not v & 3 and lo_c <= v < hi_c and v not in funcs and v not in cands:
                        if block_end(word(v - 4)) and word(v) != 0:
                            if not (word(v + 8) == 0x7D6903A6 and word(v + 12) == 0x4E800420):
                                cands.setdefault(v, []).append(va + off)
                                code_refs += 1
                    break
                if (nxt >> 21) & 31 == rd and op not in (36, 37, 38, 44, 45, 52, 53, 54, 55):
                    break  # rd overwritten by something other than a store
    print(f"{code_refs} code-built candidates")

    with open(sys.argv[3], "w") as out:
        out.write("# Code addresses reached only through data pointers (find_ptr_targets.py).\n")
        for v in sorted(cands):
            out.write(f"0x{v:08X} = {{}}  # refs: {len(cands[v])}, first at 0x{cands[v][0]:08X}\n")
    print(f"{len(cands)} candidates -> {sys.argv[3]}")
    for probe in (0x836E9A58, 0x825794B0):
        print(f"probe 0x{probe:08X}: {'found' if probe in cands else 'MISSING'}")


if __name__ == "__main__":
    main()
