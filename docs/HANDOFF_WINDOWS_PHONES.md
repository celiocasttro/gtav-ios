# Handoff (Mac -> Windows): "Build for iPhone" and "Build for Android" on Windows

Written 2026-11-01 after v0.2.0-macos (Mac + iPhone + Android builds) shipped. For a session on the
owner's Windows PC (nakas4050; not reachable from the Mac). Goal: **v0.2.x-windows** whose setup
window has working **Build for iPhone** and **Build for Android** buttons, so a Windows player can
make the phone apps from their own game, as Mac and Linux players can now.

## The rule (unchanged)

Nothing Rockstar-derived ships: no game code (recompiled or not), data or shaders. The iPhone and
Android apps are built on the player's machine from their own discs. The packages carry only our
shells (no game code), interface lists, simde and stub libraries.

## What already exists (branch android-pc-build, 224992b or later)

- `loader/src/install.cpp`: one installer for all three systems. `RunIphone` / `RunAndroid`: codegen +
  reference check, compile for `arm64-apple-ios17.0` / `aarch64-linux-android30` with the package's
  clang, link with the package's lld under any name via `-flavor darwin` / `-flavor gnu`
  (Windows: `tools\llvm\bin\lld-link.exe`), shaders through `tools\XenosRecomp.exe`, the Adreno/UBO
  passes (`gtav/src/native/adreno_shader_fix.cpp`, SPIRV-Tools linked statically) and `tools\dxc.exe`,
  then `iphone\GTAV.ipa` (own zip writer with Unix modes, skips `._*`/`.DS_Store`) or
  `android\gtav-android-pack.zip` + `GTAV.apk`. Response files use forward slashes (lld tokenizes them
  GNU-style); arguments are wide strings (`generic_wstring`).
- `loader/src/main.cpp`: **Build for iPhone** / **Build for Android** buttons next to Install/Cancel
  (`StartPhone`), `--iphone` / `--android` without a window (`data\logs\<phone>_unattended.txt`);
  Explorer opens with the result selected. Shift at launch shows the setup window when installed.
- `loader/CMakeLists.txt`: links SPIRV-Tools from `rexglue-sdk/thirdparty/spirv-tools` (needed for the
  Android shaders) and compiles `adreno_shader_fix.cpp` into the launcher.
- `tools/release/make_release.py --ios DIR --android DIR`: copies the two phone folders as they are.
- README.txt (Windows) has the iPhone steps; add Android ones like README_LINUX.txt / README_MACOS.txt.

**Checked on the Mac only**: install.cpp, sources.cpp, main.cpp and adreno_shader_fix.cpp pass a
Windows compile check with mingw-w64 GCC 16 (`-std=c++23 -fsyntax-only`, with and without
`GTAV_HAVE_SPIRV_TOOLS`). Never built with MSVC, never run on Windows.

Proven elsewhere: the same code paths ran end to end from the macOS package (iPhone: 811/811 gen,
406 files, 3,604 shaders, 0 failures; the ipa plays on an iPhone 13 mini) and from the Linux package
(Linux session: ld.lld -flavor darwin, 811/811, 3,617 shaders, 0 failures). lld under the names
`ld.lld` and `lld-link` was checked to link an iOS dylib with `-flavor darwin`.

## Work list

1. **Sync**: `git fetch && git checkout android-pc-build` (or main once merged). Re-apply the patches
   to the SDK / XenosRecomp / plume trees as for the v0.1.0-windows build (HANDOFF_WINDOWS_MAC.md):
   rexglue-sdk-gtav-linux.patch (it also carries the Windows changes now), xenosrecomp-gtav.patch
   (render-scale pixel constant), plume-gtav.patch. The codegen tool must be rebuilt: the patch adds
   memory-barrier macros (REX_SYNC/REX_LWSYNC/REX_EIEIO) and `tools/release/expected.txt` changed
   (124 of 811 gen hashes). Check `rexgluerd.exe codegen` output against it (811/811).
2. **Build** gtav_fast.exe (GTAV_GAME_DLL=ON) and the launcher with MSVC as for v0.1.0-windows. Expect
   first-time MSVC issues in install.cpp's new code (std::regex, `<thread>`, wide/narrow strings,
   `generic_wstring`) and in the SPIRV-Tools static build; mingw compiled them, MSVC has not.
3. **Package** with make_release.py and both phone folders:
   - `ios/`: from the published `gtav-recomp-macos-arm64.zip` (v0.2.0-macos),
     `GTAV.app/Contents/Resources/gtav/ios`, **without** the `._*` AppleDouble files that zip carries
     (or from a newer Mac build). It must be the 36cc9b2 shell (contains `hold_max_frames`).
   - `android/`: from `gtav-recomp-linux-x64.tar.gz` of v0.2.0-linux (or v0.2.1-linux).
   - Check `tools\llvm\lib\clang\19\include\arm_neon.h` exists in the package (clang's resource headers:
     the generated code is compiled for ARM with simde's SSE-on-NEON).
4. **Test from the unpacked package** on an installed test game (both ISOs + TU27):
   `gtav_launcher.exe --iphone` and `--android`, then the buttons. Expect ~811/811, 406 files compiled,
   3,604+ shaders 0 failed, `iphone\GTAV.ipa` ~198 MB, `android\gtav-android-pack.zip` ~150 MB. If
   shaders fail: the logs are in `data\build\spirv_ios` but that folder is deleted at the end (known
   issue: keep it on failure).
5. **Phone test**: install the Windows-made `GTAV.ipa` with Sideloadly for Windows on the owner's
   iPhone 13 mini (the game data is already installed in the app; the app's bundle id under Sideloadly
   may differ from the dev one com.nakas.gtav, in which case the setup screen asks for the discs).
   Android: the pack + APK on the S23 FE.
6. **Taint scan** (`tools\release\scan_release.cmd`) of the whole package, including ios\ and android\.
7. **Publish** v0.2.x-windows only after the owner confirms (as v0.2.0-macos was).

## Facts worth knowing

- Windows LLVM is 19.1.5 (macOS 19.1.7): both know `-mcpu=apple-a14` and the Darwin/Android targets;
  `clang.exe` is shipped as `clang++.exe`, `lld-link.exe` is the same multi-flavor lld binary.
- The ipa writer sets mode 0755 on `gtav` and `*.dylib` itself (NTFS has no Unix modes): do not zip
  the Payload with another tool.
- Players need Sideloadly / AltStore for the ipa (Windows works); a free Apple ID re-signs every
  7 days. Free-ID memory limit measured on the 13 mini: 2,097 MB (the game runs; ~1.5 GB in play).
- Phone behaviour fixed on 11-01 that a Windows build inherits (backend.cpp): failed pipelines retried,
  no frame holds for shaders a phone can never compile, the lost-frame hold limited (15 frames on
  phones), render scale 0.75 on phones.
