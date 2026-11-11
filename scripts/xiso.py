"""List or extract files from an Xbox 360 game ISO (XDVDFS).

usage: python -I xiso.py list ISO
       python -I xiso.py extract ISO OUTDIR [NAME_FILTER]
"""
import os
import struct
import sys

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
# Game partition offsets: plain XISO, XGD3, XGD1, XGD2.
PARTITIONS = (0, 0x2080000, 0xFD90000, 0x18300000)


def find_partition(f):
    for base in PARTITIONS:
        f.seek(base + 32 * SECTOR)
        if f.read(len(MAGIC)) == MAGIC:
            return base
    sys.exit("no XDVDFS volume found")


def walk(f, base, sector, size, prefix=""):
    if size == 0:
        return
    f.seek(base + sector * SECTOR)
    table = f.read(size)
    stack = [0]
    seen = set()
    while stack:
        off = stack.pop()
        if off in seen or off + 14 > len(table):
            continue
        seen.add(off)
        left, right, start, length, attr, nlen = struct.unpack_from("<HHIIBB", table, off)
        if left == 0xFFFF and right == 0xFFFF:
            continue
        name = table[off + 14:off + 14 + nlen].decode("latin-1")
        path = prefix + name
        if attr & 0x10:
            yield path + "/", start, length, True
            yield from walk(f, base, start, length, path + "/")
        else:
            yield path, start, length, False
        if left:
            stack.append(left * 4)
        if right:
            stack.append(right * 4)


def entries(f):
    base = find_partition(f)
    f.seek(base + 32 * SECTOR + 20)
    root_sector, root_size = struct.unpack("<II", f.read(8))
    return base, sorted(walk(f, base, root_sector, root_size))


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("list", "extract"):
        sys.exit(__doc__)
    with open(sys.argv[2], "rb") as f:
        base, items = entries(f)
        files = [e for e in items if not e[3]]
        if sys.argv[1] == "list":
            for path, _, length, is_dir in items:
                print(f"{'<dir>' if is_dir else length:>12}  {path}")
            print(f"{len(files)} files, {sum(e[2] for e in files) / 2**30:.2f} GiB, partition 0x{base:X}")
            return
        out = sys.argv[3]
        filt = sys.argv[4].lower() if len(sys.argv) > 4 else ""
        for path, start, length, _ in files:
            if filt not in path.lower():
                continue
            dest = os.path.join(out, *path.split("/"))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            f.seek(base + start * SECTOR)
            remaining = length
            with open(dest, "wb") as o:
                while remaining:
                    chunk = f.read(min(remaining, 16 << 20))
                    if not chunk:
                        sys.exit(f"short read in {path}")
                    o.write(chunk)
                    remaining -= len(chunk)
            print(f"{length:>12}  {path}")


if __name__ == "__main__":
    main()
