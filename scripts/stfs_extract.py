"""List or extract an Xbox 360 STFS package (CON/LIVE/PIRS).

usage: python -I stfs_extract.py list PACKAGE
       python -I stfs_extract.py extract PACKAGE OUTDIR
"""
import os
import struct
import sys

BLOCK = 0x1000
PER_HASH = 170  # data blocks covered by one hash table


class Stfs:
    def __init__(self, path):
        self.f = open(path, "rb")
        h = self.f.read(0x971A)
        self.magic = h[:4]
        if self.magic not in (b"CON ", b"LIVE", b"PIRS"):
            sys.exit(f"not an STFS package: {self.magic!r}")
        header_size = struct.unpack_from(">I", h, 0x340)[0]
        self.content_type = struct.unpack_from(">I", h, 0x344)[0]
        vd = 0x379
        flags = h[vd + 2]
        self.ft_blocks = struct.unpack_from("<H", h, vd + 3)[0]
        self.ft_start = int.from_bytes(h[vd + 5:vd + 8], "little")
        self.total_blocks = struct.unpack_from(">I", h, vd + 0x1C)[0]
        self.base = (header_size + 0xFFF) & ~0xFFF
        # Read-only (signed) packages keep one hash table per level, others two.
        self.tables = 1 if flags & 1 else 2
        self.title = h[0x411:0x411 + 0x80].decode("utf-16-be", "ignore").split("\0")[0]

    def backing_block(self, index):
        block = index
        base = PER_HASH
        for _ in range(3):
            block += ((index + base) // base) * self.tables
            if index < base:
                break
            base *= PER_HASH
        return block

    def offset(self, index):
        return self.base + self.backing_block(index) * BLOCK

    def hash_entry(self, index):
        # Level-0 hash table sits right before its group of 170 data blocks.
        group_first = index - index % PER_HASH
        table = self.backing_block(group_first) - self.tables
        if group_first >= PER_HASH:
            pass  # backing_block already skipped higher-level tables
        self.f.seek(self.base + table * BLOCK + (index % PER_HASH) * 0x18)
        return self.f.read(0x18)

    def next_block(self, index):
        return int.from_bytes(self.hash_entry(index)[0x15:0x18], "big")

    def blocks(self, start, count, contiguous):
        out, b = [], start
        for _ in range(count):
            out.append(b)
            b = b + 1 if contiguous else self.next_block(b)
        return out

    def entries(self):
        raw = b""
        for b in self.blocks(self.ft_start, self.ft_blocks, False):
            self.f.seek(self.offset(b))
            raw += self.f.read(BLOCK)
        items = []
        for i in range(0, len(raw), 0x40):
            e = raw[i:i + 0x40]
            nl = e[0x28] & 0x3F
            if nl == 0:
                break
            items.append({
                "index": i // 0x40,
                "name": e[:nl].decode("latin-1"),
                "dir": bool(e[0x28] & 0x80),
                "contiguous": bool(e[0x28] & 0x40),
                "blocks": int.from_bytes(e[0x29:0x2C], "little"),
                "start": int.from_bytes(e[0x2F:0x32], "little"),
                "parent": struct.unpack_from(">h", e, 0x32)[0],
                "size": struct.unpack_from(">I", e, 0x34)[0],
            })
        return items

    def paths(self, items):
        by_index = {e["index"]: e for e in items}

        def full(e):
            parts = [e["name"]]
            while e["parent"] != -1:
                e = by_index[e["parent"]]
                parts.append(e["name"])
            return "/".join(reversed(parts))

        return [(full(e), e) for e in items]


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("list", "extract"):
        sys.exit(__doc__)
    s = Stfs(sys.argv[2])
    items = s.paths(s.entries())
    if sys.argv[1] == "list":
        print(f"{s.magic.decode()} type 0x{s.content_type:08X} '{s.title}' tables={s.tables} "
              f"base=0x{s.base:X} filetable={s.ft_blocks}@{s.ft_start}")
        for path, e in items:
            kind = "<dir>" if e["dir"] else f"{e['size']:>12}"
            print(f"{kind:>12} {'c' if e['contiguous'] else '-'} {path}")
        return
    out = sys.argv[3]
    for path, e in items:
        dest = os.path.join(out, *path.split("/"))
        if e["dir"]:
            os.makedirs(dest, exist_ok=True)
            continue
        os.makedirs(os.path.dirname(dest) or out, exist_ok=True)
        remaining = e["size"]
        with open(dest, "wb") as o:
            for b in s.blocks(e["start"], e["blocks"], e["contiguous"]):
                if remaining <= 0:
                    break
                s.f.seek(s.offset(b))
                chunk = s.f.read(min(BLOCK, remaining))
                o.write(chunk)
                remaining -= len(chunk)
        print(f"{e['size']:>12}  {path}")


if __name__ == "__main__":
    main()
