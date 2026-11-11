"""Copy a Mach-O shader tool (XenosRecomp, dxc-macos) for use from <folder>/tools: arm64 slice
only, every LC_RPATH replaced by @executable_path/lib (where libdxcompiler.dylib goes), signed ad
hoc again (install_name_tool invalidates the signature, and arm64 macOS runs nothing unsigned).

usage: macho_tool.py SRC DST
"""
import os
import shutil
import subprocess
import sys


def rpaths(path):
    out = subprocess.run(["otool", "-l", path], capture_output=True, text=True, check=True).stdout
    lines = out.splitlines()
    return [lines[i + 2].split()[1] for i, l in enumerate(lines) if l.strip() == "cmd LC_RPATH"]


def main():
    src, dst = sys.argv[1:3]
    tmp = dst + ".tmp"
    archs = subprocess.run(["lipo", "-archs", src], capture_output=True, text=True).stdout.split()
    if len(archs) > 1:
        subprocess.run(["lipo", src, "-thin", "arm64", "-output", tmp], check=True)
    else:
        shutil.copy2(src, tmp)
    for r in rpaths(tmp):
        subprocess.run(["install_name_tool", "-delete_rpath", r, tmp], check=True,
                       stderr=subprocess.DEVNULL)
    subprocess.run(["install_name_tool", "-add_rpath", "@executable_path/lib", tmp], check=True)
    subprocess.run(["codesign", "--force", "--sign", "-", tmp], check=True, capture_output=True)
    os.chmod(tmp, 0o755)
    os.replace(tmp, dst)


if __name__ == "__main__":
    main()
