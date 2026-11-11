# v0.3.0 build sheet: one release, three desktop packages, each able to build iPhone and Android

Written 2026-11-01 on the Mac. **Read all of it before building.** One GitHub release, tag `v0.3.0`,
with every package built from the **same commit**: `main` as of the Windows build (f36486f or later,
which adds the app-local Visual C++ runtime). Record the commit you built from in the draft's notes;
the Mac rebuilds its package from that commit during final assembly.

Order (the owner's plan): the Windows PC builds the Windows package, then reboots into Linux and
builds the Linux package and the Android folder; the Mac builds the Mac package and the iPhone shell.
Nothing is published until the owner says so: assets go to a **draft** release.

## Assets of the release

| asset | made on | contents |
|---|---|---|
| `gtav-recomp-windows-x64.zip` | Windows | launcher, runtime, tools, `ios/`, `android/` |
| `gtav-recomp-linux-x64.tar.gz` | Linux | the same for Linux |
| `gtav-recomp-macos-arm64.zip` | Mac | `GTAV.app` with `ios/` and `android/` inside |
| `gtav-recomp-src-v0.3.0.tar.xz` | any (one) | complete corresponding source (make_source_archive.sh) |

All three packages carry the **identical** `ios/` folder (iPhone shell, Mac-built) and the
**identical** `android/` folder (APK + link libs, Linux-built). Version strings: Mac app 0.3.0,
iPhone app 0.3.0, Android versionName 0.3.0 / versionCode 3.

## Draft release (shared between the machines)

The Mac creates the draft `v0.3.0` (target = the release commit) and uploads
`ios-shell-v0.3.0.tar` (the `ios/` folder; no game code; removed from the release before publishing).
Every machine adds its assets with `gh release upload v0.3.0 <file> --clobber` and reads the others
with `gh release download v0.3.0 -p <pattern>` (drafts are visible to the repo owner's gh login).

## 1. Mac (this machine): ios/ and the Mac package

1. iOS shell from the release commit: rebuild `build/release-ios-shell` (SDK `rexglue-sdk/out/install/ios-rel`,
   `-mcpu=apple-a14 -ffile-prefix-map`), `tools/ios/make_shell.sh OUT` (refuses prebuilt native
   shaders; checks no build paths). Tar it without AppleDouble files (`COPYFILE_DISABLE=1`), upload.
2. After Linux uploads `android-v0.3.0.tar` (step 3.4): `build_release_macos.sh OUT LLVM LIC
   --ios-shell <ios> --android <android> --zip` (the zip is written without `._*` entries now).
3. Checks from the **unzipped** package with a fresh game folder linked to an installed game:
   `GTAV --iphone`, `GTAV --android` (811/811 gen, 0 failed shaders), the ipa installed on the iPhone
   13 mini, `scan_release_macos.sh` with the gen dir, iPhone/Android spirv and a built game dylib as
   sources (PASS).

## 2. Windows (first boot)

1. `git fetch && git checkout <release commit>`; re-apply `rexglue-sdk-gtav-linux.patch`,
   `xenosrecomp-gtav.patch`, `plume-gtav.patch` to their trees; rebuild the codegen tool, SDK runtime,
   gtav_fast.exe (GTAV_GAME_DLL=ON) and the launcher (`build_launcher.cmd`; SPIRV-Tools via
   GTAV_SDK_THIRDPARTY as in 4784105).
2. Codegen check: 811/811 against `tools/release/expected.txt`.
3. `gh release download v0.3.0 -p ios-shell-v0.3.0.tar` and unpack it. For `android/`, the first
   Windows package may use the v0.2.1-linux `android/` **only as a stand-in**: the final zip must
   carry the Linux-built v0.3.0 `android/` (step 3.4), see step 4.
4. `make_release.py OUT --llvm ... --expected ... --ios <ios> --android <android> --zip`, from a VS x64
   developer prompt (or `--vcruntime <...\\Microsoft.VC143.CRT>`): **new in 0.3.0** the Visual C++
   runtime DLLs are copied next to the exes and into tools\\, so players need no redistributable.
   Check on a Windows without the VC++ redistributable installed (or a fresh VM) that
   gtav_launcher.exe, gtav_fast.exe, XenosRecomp.exe and dxc.exe start.
5. From the unpacked package on an installed test game: `gtav_launcher.exe --iphone` and `--android`
   (0 failed; the 4784105 handle fix matters here), the Windows-made ipa on the iPhone.
6. `tools\release\scan_release.cmd` (PASS), upload `gtav-recomp-windows-x64.zip` to the draft.
   Write down which `android/` it carries.

## 3. Linux (second boot, same PC)

1. Same commit and patches; build the runtime, launcher, `gtav_fast` (GTAV_GAME_DLL=ON), as for
   v0.2.0-linux (`tools/release/compat/build.sh` for the glibc 2.35 floor).
   **New in 0.3.0, if it can be done safely**: carry libstdc++.so.6 and libgcc_s.so.1 (GCC 13+) in the
   package with an `$ORIGIN` runpath, so Ubuntu 22.04 / Debian 12 work without the toolchain PPA
   (check_needed then only allows glibc and the desktop libraries). Update README_LINUX.txt and
   the repo README's requirements row if you do.
2. **Android from this commit**: libgtav_fast.so / libmain.so shim / APK (`tools/android/build_apk.sh`,
   versionCode 3), then `tools/android/make_shell.sh dist/android`. This carries the phone renderer
   fixes from 11-01 (failed-pipeline retry, no holds for shaders that can't arrive, hold limit, render
   scale on iOS only): **test it on the S23 FE** with a pack made by `--android` before release.
3. `gh release download v0.3.0 -p ios-shell-v0.3.0.tar`; `make_release_linux.py OUT --llvm ...
   --ios <ios> --android dist/android --tar`.
4. Upload `android-v0.3.0.tar` (dist/android, tarred) to the draft: the Mac and Windows packages take
   it from there.
5. Checks from the unpacked tarball: `--iphone`, `--android` (0 failed), taint scan (PASS), upload
   `gtav-recomp-linux-x64.tar.gz`.

## 4. Final assembly (any machine with all assets)

- If the Windows zip carries the v0.2.1 stand-in `android/`: replace that folder inside the zip with
  the v0.3.0 one (unzip, swap the folder, zip again with Python zipfile; nothing else changes), or
  rebuild the Windows package on its next Windows boot. Check: the three packages' `android/GTAV.apk`
  and `ios/GTAV.app/gtav` have equal SHA-256.
- Source archive: `tools/release/make_source_archive.sh gtav-recomp-src-v0.3.0.tar.xz v0.3.0 rexglue-sdk
  thirdparty/XenosRecomp thirdparty/plume` (needs the tag: create it locally at the release commit).
- Delete `ios-shell-v0.3.0.tar` and `android-v0.3.0.tar` from the draft, write the notes (what each
  package does, phone steps, checks, SHA-256 of every asset), and **ask the owner**. Publishing pushes
  the tag `v0.3.0` and makes the release public and Latest.

## Notes for the release text

- One download per computer; every package can build the iPhone ipa and the Android pack.
- Requirements and steps: the repo README (rewritten for 0.3.0) and each package's README.
- Measured: M1 ~45-60 fps; iPhone 13 mini ~30 fps (city, prologue), ~20 in the heaviest scenes,
  slower when hot; S23 FE ~28-29 fps cool, 14-17 throttled.
- Disclose as benign, as v0.2.1-linux did: the shells' hook-name table (`__imp__sub_<address>`,
  addresses from our manifest) and the SDK's kernel-export names.
