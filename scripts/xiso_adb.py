"""Extract files from an Xbox 360 ISO that sits on an Android phone, without copying the ISO.

Reads byte ranges over `adb exec-out dd` (XDVDFS files are contiguous), so only the extracted
files need local space. Files already present with the right size are skipped.

usage: python -I xiso_adb.py list /sdcard/Download/gtav_isos/<disc>.iso
       python -I xiso_adb.py extract /sdcard/Download/gtav_isos/<disc>.iso OUTDIR [NAME_FILTER]
"""
import os
import shlex
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # -I drops the script dir
import xiso  # noqa: E402

SECTOR = xiso.SECTOR


class AdbFile:
    """Seekable read-only view of a file on the phone (reads whole sectors through dd)."""

    def __init__(self, path):
        self.path = path
        self.pos = 0

    def seek(self, pos):
        self.pos = pos

    def read(self, size):
        first = self.pos // SECTOR
        last = (self.pos + size + SECTOR - 1) // SECTOR
        data = self.read_sectors(first, last - first)
        skip = self.pos - first * SECTOR
        out = data[skip:skip + size]
        self.pos += len(out)
        return out

    def read_sectors(self, first, count):
        cmd = f"dd if={shlex.quote(self.path)} bs={SECTOR} skip={first} count={count} 2>/dev/null"
        return subprocess.run(["adb", "exec-out", cmd], check=True, capture_output=True).stdout

    def stream(self, offset, length, out):
        """Copies length bytes at a sector-aligned offset to out, in 64 MiB dd runs."""
        assert offset % SECTOR == 0
        first, remaining = offset // SECTOR, length
        while remaining:
            n = min(remaining, 64 << 20)
            data = self.read_sectors(first, (n + SECTOR - 1) // SECTOR)[:n]
            if len(data) != n:
                sys.exit(f"short read at sector {first}")
            out.write(data)
            first += n // SECTOR
            remaining -= n


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("list", "extract"):
        sys.exit(__doc__)
    f = AdbFile(sys.argv[2])
    base, items = xiso.entries(f)
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
        if os.path.exists(dest) and os.path.getsize(dest) == length:
            print(f"{length:>12}  {path} (present)")
            continue
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest + ".part", "wb") as o:
            f.stream(base + start * SECTOR, length, o)
        os.replace(dest + ".part", dest)
        print(f"{length:>12}  {path}", flush=True)


if __name__ == "__main__":
    main()
