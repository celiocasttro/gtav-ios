"""Check a release folder for game-derived content before publishing.

1. Names: no file with a game/console extension, generated-source name or retail file name.
2. Bytes: 32-byte windows of retail-derived files (decrypted executable image, dumped shaders,
   generated C++) must not appear in any release file. Windows are picked by content (a rolling
   hash whose low bits are zero), so a copy is found at any offset and alignment. Windows of
   fewer than 12 distinct bytes (padding, runs) are ignored.

   Windows that also occur in a control file (Windows system DLLs, the SDK sources: no game in
   them) are public constants and code-generator templates (CRC and AES tables, ctype tables,
   the PE stub, kernel export names) and do not count.

usage: taint_scan.py RELEASE_DIR SOURCE [SOURCE ...] [--control PATH ...]
  SOURCE  a retail-derived file or folder (folders are walked; .cpp/.h/.bin/.xex and similar)
  PATH    a file or folder known to hold no game data (folders: .dll/.exe/.lib/.cpp/.h/.inc)
Prints one summary; exit code 1 when anything is found.
"""
import os
import re
import sys
from pathlib import Path

import numpy as np

W = 32          # window length
MASK = 31       # keep windows whose hash has these bits zero (1 in 32)
CHUNK = 64 << 20
MIN_DISTINCT = 12
MAX_RUN = 128   # a common run this long fails the scan

BAD_EXT = {".xex", ".xexp", ".rpf", ".iso", ".spv", ".bin", ".pdb", ".timestamp", ".hlsl", ".dmp", ".stfs"}
BAD_NAME = re.compile(r"^(gtav_recomp\.\d+\.cpp|gtav_init\.(cpp|h)|gtav_config\.h|default\.xexp?|update\.rpf|"
                      r"part\d+\.rpf|tu0000000\d_00000000|basefile.*|vk_pipelines\.bin|[pv]s_[0-9A-F]{16}.*)$",
                      re.I)
SOURCE_EXT = {".cpp", ".h", ".bin", ".xex", ".xexp", ".hlsl", ".spv"}

# Random odd multipliers, one per window position: h = sum(b[i+k] * M[k]) mod 2^64.
MULT = (np.random.default_rng(1234).integers(1, 2**62, W, dtype=np.uint64) * 2 + 1).astype(np.uint64)


def window_hashes(data):
    """(hash, offset) of the selected windows of a bytes object."""
    hs, offs = [], []
    n = len(data)
    for start in range(0, max(n - W + 1, 0), CHUNK):
        end = min(n, start + CHUNK + W - 1)
        b = np.frombuffer(data, dtype=np.uint8, count=end - start, offset=start).astype(np.uint64)
        m = len(b) - W + 1
        if m <= 0:
            break
        h = np.zeros(m, dtype=np.uint64)
        with np.errstate(over="ignore"):
            for k in range(W):
                h += b[k:k + m] * MULT[k]
        sel = np.nonzero((h >> np.uint64(17)) & np.uint64(MASK) == 0)[0]
        hs.append(h[sel])
        offs.append(sel + start)
    if not hs:
        return np.zeros(0, np.uint64), np.zeros(0, np.int64)
    return np.concatenate(hs), np.concatenate(offs)


def arithmetic(win):
    """Counting tables (0,1,2,... as 16- or 32-bit values, either byte order, any phase) carry no
    content and are common to every runtime library."""
    for size in (1, 2, 4):
        for phase in range(size):
            for order in ("little", "big"):
                v = [int.from_bytes(win[i:i + size], order) for i in range(phase, len(win) - size + 1, size)]
                d = {(b - a) % (1 << (8 * size)) for a, b in zip(v, v[1:])}
                if len(d) == 1:
                    return True
    return False


def distinct_ok(data, off):
    win = data[off:off + W]
    return len(set(win)) >= MIN_DISTINCT and not arithmetic(win)


def public_tables():
    """Standard tables computed from their definitions: CRC-32 (both byte orders, alone and as
    zlib's eight braided tables) and the AES S-box and inverse S-box. Every runtime that checks or
    decrypts data carries them."""
    crc = []
    for n in range(256):
        c = n
        for _ in range(8):
            c = (c >> 1) ^ 0xEDB88320 if c & 1 else c >> 1
        crc.append(c)
    sbox, inv = [0] * 256, [0] * 256
    p = q = 1
    while True:  # p walks the multiplicative group by 3, q = p^-1
        p = p ^ ((p << 1) & 0xFF) ^ (0x1B if p & 0x80 else 0)
        q ^= q << 1
        q ^= q << 2
        q ^= q << 4
        q &= 0xFF
        if q & 0x80:
            q ^= 0x09
        x = q ^ ((q << 1 | q >> 7) & 0xFF) ^ ((q << 2 | q >> 6) & 0xFF) ^ ((q << 3 | q >> 5) & 0xFF) ^ \
            ((q << 4 | q >> 4) & 0xFF)
        sbox[p] = x ^ 0x63
        if p == 1:
            break
    sbox[0] = 0x63
    for i, v in enumerate(sbox):
        inv[v] = i
    # zlib (1.2.x, BYFOUR) keeps eight CRC tables back to back: table k+1 = one more byte through
    # table 0. The next table starts with 0, so a lone table followed by zero bytes (dxc) matches
    # the start of zlib's block (the game) for 4 bytes past the first table.
    braided = [crc]
    for _ in range(7):
        braided.append([(v >> 8) ^ crc[v & 0xFF] for v in braided[-1]])
    zlib = [b"".join(c.to_bytes(4, o) for t in braided for c in t) for o in ("little", "big")]
    return [b"".join(c.to_bytes(4, o) for c in crc) for o in ("little", "big")] + zlib + \
        [bytes(sbox), bytes(inv)]


def spirv_preamble(data):
    """Length of a SPIR-V module's header + OpCapability/OpExtension/OpExtInstImport/OpMemoryModel
    run: written by DXC into every module, ours included."""
    if data[:4] != b"\x03\x02\x23\x07":
        return 0
    off = 20
    while off + 4 <= len(data):
        word = int.from_bytes(data[off:off + 4], "little")
        op, count = word & 0xFFFF, word >> 16
        if op not in (17, 10, 11, 14) or count == 0:
            break
        off += 4 * count
    return off


# Generated files that are the SDK's own templates, not translated game code, and the release's
# own headers that build_game_dll.py copies into the generated folder (layout_check.h, gtav_hooks.h).
SDK_TEMPLATE_FILES = {"gtav_pch.h", "layout_check.h", "gtav_hooks.h"}


def content_length(data, a, n):
    """Bytes of a run covered by non-trivial windows (padding and counting tables do not count)."""
    return sum(8 for k in range(0, n - W + 1, 8) if distinct_ok(data, a + k))


CONTROL_EXT = {".dll", ".exe", ".lib", ".cpp", ".h", ".hpp", ".c", ".inc", ".inja"}


def walk(p, exts=SOURCE_EXT):
    p = Path(p)
    if p.is_file():
        yield p
    else:
        for root, _, files in os.walk(p):
            for f in files:
                q = Path(root) / f
                if q.suffix.lower() in exts:
                    yield q


def main():
    args = sys.argv[1:]
    controls = []
    if "--control" in args:
        i = args.index("--control")
        args, controls = args[:i], args[i + 1:]
    if len(args) < 2:
        sys.exit(__doc__)
    rel = Path(args[0])
    sources = args[1:]
    problems = []

    files = [p for p in rel.rglob("*") if p.is_file()]
    for p in files:
        if p.suffix.lower() in BAD_EXT or BAD_NAME.match(p.name):
            problems.append(f"name: {p.relative_to(rel)}")

    # Index the release: hash -> (file, offset), only windows with enough distinct bytes.
    index = {}
    for p in files:
        data = p.read_bytes()
        hs, offs = window_hashes(data)
        for h, o in zip(hs.tolist(), offs.tolist()):
            if h not in index and distinct_ok(data, o):
                index[h] = (p, o)
    keys = np.fromiter(index.keys(), dtype=np.uint64, count=len(index))
    keys.sort()
    print(f"release: {len(files)} files, {len(keys)} windows indexed", flush=True)

    # Release windows that a control file also holds are generic: drop them from the index.
    generic = set()
    def blobs():
        yield from public_tables()
        for c in controls:
            for p in walk(c, CONTROL_EXT):
                try:
                    yield p.read_bytes()
                except OSError:
                    pass
    for blob in blobs():
        hs, _ = window_hashes(blob)
        pos = np.searchsorted(keys, hs)
        pos[pos >= len(keys)] = 0
        generic.update(hs[keys[pos] == hs].tolist() if len(keys) else [])
    keys = keys[~np.isin(keys, np.fromiter(generic, dtype=np.uint64, count=len(generic)))]
    print(f"control: {len(generic)} release windows are generic", flush=True)

    # Each match is grown to the full common run. Game content copied into a release file shows as
    # long runs; boilerplate shared with retail-derived files (SPIR-V extension names, the code
    # generator's per-instruction templates) stays in short runs.
    longest = {}  # release file -> (run length, release offset, source, source offset)
    short = {}
    cache = {}
    scanned = 0
    for src in sources:
        print(f"  scanning {src}", flush=True)
        for p in walk(src):
            if p.name in SDK_TEMPLATE_FILES:
                continue
            data = p.read_bytes()
            scanned += len(data)
            hs, offs = window_hashes(data)
            keep = offs >= spirv_preamble(data)
            hs, offs = hs[keep], offs[keep]
            pos = np.searchsorted(keys, hs)
            pos[pos >= len(keys)] = 0
            found = np.nonzero(keys[pos] == hs)[0] if len(keys) else []
            covered = {}  # (release file, offset delta) -> end of the run already grown
            for i in found:
                h, o = int(hs[i]), int(offs[i])
                rp, ro = index[h]
                if covered.get((rp, ro - o), -1) > o:
                    short[rp] += 1  # inside a run already measured
                    continue
                rd = cache.get(rp) or cache.setdefault(rp, rp.read_bytes())
                if rd[ro:ro + W] != data[o:o + W]:
                    continue
                a, b = ro, o
                while a > 0 and b > 0 and rd[a - 1] == data[b - 1]:
                    a, b = a - 1, b - 1
                n = 0
                while a + n < len(rd) and b + n < len(data) and rd[a + n] == data[b + n]:
                    n += 1
                covered[(rp, a - b)] = b + n
                n = content_length(rd, a, n)
                short[rp] = short.get(rp, 0) + 1
                if n > longest.get(rp, (0,))[0]:
                    longest[rp] = (n, a, p, b)
    print(f"sources: {scanned / 2**20:.0f} MB scanned")
    for rp, (n, a, p, b) in sorted(longest.items()):
        line = (f"{rp.relative_to(rel)}: {short[rp]} matching windows, longest run {n} bytes "
                f"(@{a:#x} = {p.name}@{b:#x})")
        if n >= MAX_RUN:
            problems.append("bytes: " + line)
        else:
            print("  short runs only (boilerplate): " + line)
    if problems:
        print(f"FAIL: {len(problems)} problem(s)")
        for line in problems[:60]:
            print("  " + line)
        sys.exit(1)
    print("PASS: 0 failing")


if __name__ == "__main__":
    main()
