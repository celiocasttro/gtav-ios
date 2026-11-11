"""Write the launcher's reference list (tools/game_dll/expected.txt in a release).

Each line is "<kind> <size> <sha256> <path>": sizes and hashes of a known good dump and of the
codegen output, so the launcher can tell the player when a file is damaged or different. The
list holds no game data, only hashes and file names.

usage: make_expected.py data DISC2_DIR USERDATA_DIR TU_DIR OUT     (disc2, part and tu lines)
       make_expected.py gen GENERATED_DIR OUT                      (gen lines)
  DISC2_DIR     extracted Disc 2 (files under $SystemUpdate and default.xexp are skipped)
  USERDATA_DIR  holds 0000000000000000/545408A7/00000002/545408A70000000N/ (install partitions)
  TU_DIR        the TU's disc002 folder (default.xexp, update.rpf)
"""
import hashlib
import os
import sys

# Codegen outputs the launcher does not compile or replaces with its own.
GEN_SKIP = {"gtav_pch.h", "gtav_register.cpp", "sources.cmake", "codegen.d", "codegen.stamp",
            "codegen.build.stamp", "codegen.partition.json"}


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(16 << 20):
            h.update(chunk)
    return h.hexdigest()


def line(kind, path, rel):
    return f"{kind} {os.path.getsize(path)} {sha(path)} {rel}\n"


def walk(root):
    for dirpath, _, files in os.walk(root):
        for f in files:
            full = os.path.join(dirpath, f)
            yield full, os.path.relpath(full, root).replace(os.sep, "/")


def main():
    mode = sys.argv[1]
    lines = []
    if mode == "data":
        disc2, user, tu, out = sys.argv[2:6]
        for full, rel in sorted(walk(disc2), key=lambda x: x[1]):
            if rel.startswith("$SystemUpdate/") or rel == "default.xexp":
                continue
            lines.append(line("disc2", full, rel))
            print(rel, flush=True)
        parts = os.path.join(user, "0000000000000000", "545408A7", "00000002")
        for full, rel in sorted(walk(parts), key=lambda x: x[1]):
            lines.append(line("part", full, rel))
            print(rel, flush=True)
        for name in ("default.xexp", "update.rpf"):
            lines.append(line("tu", os.path.join(tu, name), name))
    elif mode == "gen":
        gen, out = sys.argv[2:4]
        for full, rel in sorted(walk(gen), key=lambda x: x[1]):
            if "/" in rel or rel in GEN_SKIP:
                continue
            lines.append(line("gen", full, rel))
    else:
        sys.exit(__doc__)
    with open(out, "w", newline="\n") as f:
        f.writelines(lines)
    print(f"{len(lines)} lines -> {out}")


if __name__ == "__main__":
    main()
