"""Generate the interface between gtav_fast.exe and the game DLL (GTAV_GAME_DLL build).

Runs as the exe's pre-link step. Reads the exe's object files and writes into OUT_DIR:
  gtav_hooks.h    #define GTAVFS_HOOKED_<name> for every guest function the exe replaces
  gtav_game.def   what the exe imports from gtav_game.dll (hook originals and direct calls)
  gtav_game.lib   import library built from gtav_game.def (linked into the exe)
  gtav_fast.def   what the exe exports to the DLL (the hooks and the gtavfs_* bridge)
  rexruntime.def  the guest kernel exports (__imp__<export>) of the ReXGlue runtime DLL
  ucrtbase.def    the one C runtime function the DLL takes from Windows' own ucrtbase.dll
All of them hold names only (sub_<guest address>, our own function names), no code or data.

Linux writes, instead of the .def files and import libraries:
  gtav_game.exports   version script for gtav_game.so: exports what gtav_fast imports, the rest local
  gtav_fast.dynlist   dynamic list for gtav_fast: the hooks and the gtavfs_* bridge
  stub/gtav_game.so   an empty library with the same exports (soname gtav_game.so) that gtav_fast
                      links against; the installer's real gtav_game.so replaces it at run time
Kernel imports and log2f stay undefined in gtav_game.so; the dynamic loader binds them to
librexruntime.so and libm.so.6, which gtav_fast already loads.

macOS writes:
  gtav_game.exports   exported symbols list for gtav_game.dylib (ld64 -exported_symbols_list)
  gtav_game.undefs    the hooks and the gtavfs_* bridge: undefined in gtav_game.dylib, bound to
                      gtav_fast at load time (one -U each; Mach-O cannot link against an executable)
  stub/gtav_game.dylib  an empty library with the same exports (install name @rpath/gtav_game.dylib)
Mach-O symbol names carry a leading underscore; the lists use them, gtav_hooks.h does not.

usage: gen_interface.py LLVM_BIN OBJ_DIR OUT_DIR RUNTIME_DLL [CC | --android]
  LLVM_BIN  folder with llvm-nm (Linux: llvm-nm, llvm-nm-20 or llvm-nm-19; macOS: llvm-nm or nm)
  CC        Linux and macOS: the C/C++ compiler that builds the stub library
"""
import os
import re
import subprocess
import sys

KERNEL = re.compile(r"^__imp__(?!sub_)[A-Za-z0-9_]+$")
GUEST = re.compile(r"^(__imp__)?sub_[0-9A-F]{8}$")
BRIDGE = re.compile(r"^gtavfs_[a-z_]+$")


def symbols(nm, objs):
    defined, undefined = set(), set()
    # Batch the object list: the command line limit is 32k characters.
    for i in range(0, len(objs), 40):
        out = subprocess.run([nm, "--no-sort"] + objs[i:i + 40], capture_output=True, text=True,
                             check=True).stdout
        for line in out.splitlines():
            parts = line.split()
            if len(parts) == 2 and parts[0] == "U":
                undefined.add(parts[1])
            elif len(parts) == 3 and parts[1] in "TtDdBbRr":
                defined.add(parts[2])
    return defined, undefined


def write_if_changed(path, text):
    if os.path.exists(path) and open(path, encoding="ascii").read() == text:
        return False
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write(text)
    return True


def find_tool(llvm_bin, names):
    for n in names:
        p = os.path.join(llvm_bin, n)
        if os.path.isfile(p):
            return p
    sys.exit(f"gen_interface: none of {names} in {llvm_bin}")


def write_linux(out_dir, hooks, imports, bridge, cc):
    exports = imports + ["gtav_game_image_config"]
    write_if_changed(os.path.join(out_dir, "gtav_game.exports"),
                     "{\n  global:\n" + "".join(f"    {s};\n" for s in exports) +
                     "  local: *;\n};\n")
    write_if_changed(os.path.join(out_dir, "gtav_fast.dynlist"),
                     "{\n" + "".join(f"  {s};\n" for s in hooks + bridge) + "};\n")
    stub_dir = os.path.join(out_dir, "stub")
    os.makedirs(stub_dir, exist_ok=True)
    asm = os.path.join(stub_dir, "gtav_game_stub.s")
    stub = os.path.join(stub_dir, "gtav_game.so")
    changed = write_if_changed(asm, "\t.text\n" + "".join(
        f"\t.globl {s}\n\t.type {s},@function\n{s}:\n\tud2\n" for s in exports) +
        "\t.section .note.GNU-stack,\"\",@progbits\n")
    if changed or not os.path.exists(stub):
        subprocess.run([cc, "-shared", "-nostdlib", "-Wl,-soname,gtav_game.so", asm, "-o", stub],
                       check=True)


def write_macos(out_dir, hooks, imports, bridge, cc):
    exports = imports + ["gtav_game_image_config"]
    write_if_changed(os.path.join(out_dir, "gtav_game.exports"), "".join(f"_{s}\n" for s in exports))
    write_if_changed(os.path.join(out_dir, "gtav_game.undefs"),
                     "".join(f"_{s}\n" for s in hooks + bridge))
    stub_dir = os.path.join(out_dir, "stub")
    os.makedirs(stub_dir, exist_ok=True)
    asm = os.path.join(stub_dir, "gtav_game_stub.s")
    stub = os.path.join(stub_dir, "gtav_game.dylib")
    changed = write_if_changed(asm, "\t.text\n" + "".join(
        f"\t.globl _{s}\n\t.p2align 2\n_{s}:\n\tbrk #1\n" for s in exports))
    if changed or not os.path.exists(stub):
        # ld64 wants libSystem in every dylib: the toolchain's clang has no default SDK path.
        # GTAV_STUB_SDK=iphoneos: the iOS shell (the stub must be built for the exe's platform).
        sdk_name = os.environ.get("GTAV_STUB_SDK", "macosx")
        sdk = subprocess.run(["xcrun", "--sdk", sdk_name, "--show-sdk-path"], capture_output=True, text=True,
                             check=True).stdout.strip()
        target = ["-target", "arm64-apple-ios17.0"] if sdk_name == "iphoneos" else ["-arch", "arm64"]
        subprocess.run([cc, "-dynamiclib"] + target + ["-isysroot", sdk, "-install_name",
                        "@rpath/gtav_game.dylib", asm, "-o", stub], check=True)


def main():
    llvm_bin, obj_dir, out_dir, runtime_dll = sys.argv[1:5]
    linux = sys.platform.startswith("linux")
    macos = sys.platform == "darwin"
    if macos:
        nm = find_tool(llvm_bin, ["llvm-nm", "nm"])
    elif linux:
        nm = find_tool(llvm_bin, ["llvm-nm", "llvm-nm-20", "llvm-nm-19"])
    else:
        nm = os.path.join(llvm_bin, "llvm-nm.exe")
        lld = os.path.join(llvm_bin, "lld-link.exe")
    objs = []
    for root, _, files in os.walk(obj_dir):
        objs += [os.path.join(root, f) for f in files
                 if f.endswith(".o" if linux or macos else ".obj")]
    defined, undefined = symbols(nm, sorted(objs))
    if macos:
        defined = {s[1:] for s in defined if s.startswith("_")}
        undefined = {s[1:] for s in undefined if s.startswith("_")}
    hooks = sorted(s for s in defined if GUEST.match(s) and not s.startswith("__imp__"))
    imports = sorted(s for s in undefined - defined if GUEST.match(s))
    bridge = sorted(s for s in defined if BRIDGE.match(s))
    if not hooks or not imports or not bridge:
        sys.exit(f"gen_interface: suspicious symbol sets: {len(hooks)} hooks, "
                 f"{len(imports)} imports, {len(bridge)} bridge functions")

    os.makedirs(out_dir, exist_ok=True)
    write_if_changed(os.path.join(out_dir, "gtav_hooks.h"),
                     "// gtav - generated by tools/game_dll/gen_interface.py: guest functions "
                     "gtav_fast.exe replaces.\n#pragma once\n" +
                     "".join(f"#define GTAVFS_HOOKED_{h} ~, 1\n" for h in hooks))
    if len(sys.argv) > 5 and sys.argv[5] == "--android":
        # Version script for libgtav_game.so only: libgtav_fast.so leaves the game functions
        # undefined (no stub) and exports the hooks and the bridge by default visibility.
        exports = imports + ["gtav_game_image_config"]
        write_if_changed(os.path.join(out_dir, "gtav_game.exports"),
                         "{\n  global:\n" + "".join(f"    {s};\n" for s in exports) +
                         "  local: *;\n};\n")
        print(f"gen_interface: {len(hooks)} hooks, {len(imports)} imports, {len(bridge)} bridge")
        return
    if macos:
        if len(sys.argv) < 6:
            sys.exit("gen_interface: macOS needs CC (the compiler that builds the stub library)")
        write_macos(out_dir, hooks, imports, bridge, sys.argv[5])
        print(f"gen_interface: {len(hooks)} hooks, {len(imports)} imports, {len(bridge)} bridge")
        return
    if linux:
        if len(sys.argv) < 6:
            sys.exit("gen_interface: Linux needs CC (the compiler that builds the stub library)")
        write_linux(out_dir, hooks, imports, bridge, sys.argv[5])
        print(f"gen_interface: {len(hooks)} hooks, {len(imports)} imports, {len(bridge)} bridge")
        return
    game_def = os.path.join(out_dir, "gtav_game.def")
    changed = write_if_changed(game_def, "LIBRARY gtav_game.dll\nEXPORTS\n" +
                               "".join(f"  {s}\n" for s in imports + ["gtav_game_image_config"]))
    write_if_changed(os.path.join(out_dir, "gtav_fast.def"),
                     "NAME gtav_fast.exe\nEXPORTS\n" + "".join(f"  {s}\n" for s in hooks + bridge))
    exports = subprocess.run([os.path.join(llvm_bin, "llvm-readobj.exe"), "--coff-exports",
                              runtime_dll], capture_output=True, text=True, check=True).stdout
    kernel = sorted({l.split()[-1] for l in exports.splitlines()
                     if l.strip().startswith("Name:") and KERNEL.match(l.split()[-1])})
    if len(kernel) < 100:
        sys.exit(f"gen_interface: only {len(kernel)} kernel exports in {runtime_dll}")
    write_if_changed(os.path.join(out_dir, "rexruntime.def"),
                     f"LIBRARY {os.path.basename(runtime_dll)}\nEXPORTS\n" +
                     "".join(f"  {s}\n" for s in kernel))
    write_if_changed(os.path.join(out_dir, "ucrtbase.def"), "LIBRARY ucrtbase.dll\nEXPORTS\n  log2f\n")
    game_lib = os.path.join(out_dir, "gtav_game.lib")
    if changed or not os.path.exists(game_lib):
        subprocess.run([lld, "/lib", f"/def:{game_def}", f"/out:{game_lib}", "/machine:x64"],
                       check=True)
    print(f"gen_interface: {len(hooks)} hooks, {len(imports)} imports, {len(bridge)} bridge")


if __name__ == "__main__":
    main()
