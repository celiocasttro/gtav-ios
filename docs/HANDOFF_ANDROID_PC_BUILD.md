# Handoff (Mac -> Linux/Android): "Build for Android" from any PC + in-app game data setup

Written 2026-11-01 on the Mac at the end of the iPhone release work. For the Linux session that
owns the Android port (branch `android`, Galaxy S23 FE). Read this, then `docs/IOS_PORT.md`
sections "Release" and "Performance and memory", then `loader/src/install.cpp` (`RunIphone`,
`RunData`, `WriteIpa`).

## What the owner wants

The same thing the iPhone now has, for Android: a player with **any PC (Windows, Linux or Mac)**
presses one button (~15 min), and the phone then installs the game files itself from the player's
own discs. "Dead simple" for the player.

## The rule (unchanged, and why it matters here)

Nothing Rockstar-derived is ever published: no game code (recompiled or not), no game data, no
shaders. The owner asked whether to ship recompiled code inside the app as the Skate 3 iOS/Android
releases do (their ipa holds ~87,000 recompiled EA functions). Decision: **no** - Take-Two had
re3/reVC (rebuilt GTA III/VC code + player's own data) taken down and sued the developers in 2021.
So the game library is always built on the player's machine, as on every other platform.

## What exists now (branch `ios-android`, on top of `origin/android`; push status: see the end)

- `loader/src/install.cpp` - one installer for all desktop systems, now with:
  - `RunIphone` (any host): codegen + reference check (same as install), compile the generated
    code with the package's clang for another target (`ios_` flag in `Compile()` picks flags,
    include dirs and the hook header from the package's `ios/` folder), link with the package's
    lld under any name using `-flavor darwin`, translate every shader in `data/shader_cache`
    (+ `iphone/shader_cache` pulled back from the phone) with XenosRecomp + `UboConstants()`
    (C++ port of `tools/android/ubo_constants.py`, outputs byte-identical to the Python) + dxc,
    put everything into the app, pack `iphone/GTAV.ipa` with `WriteIpa` (our own stored-zip
    writer with Unix modes). A second run with a current app only redoes shaders.
  - `RunData` / `RunDataInstall(home, expected, inputs, ui)`: **game data only** - Disc 2 + TU27
    into `home/data/{game,update}`, Disc 1 install partitions into `home/data/user`, every file
    SHA-256-checked against `expected.txt`, resumable (`install_state.txt`), marker
    `data/data_installed.txt`; `IsDataInstalled(home)`. Tested on the Mac from the real ISOs:
    15.3 GB, all match. This is what a phone runs.
  - `LOADER_NO_SPAWN` (iOS): install.cpp + sources.cpp compile into the phone exe; nothing that
    spawns a process runs there. Android: bionic has no `posix_spawn_file_actions_addchdir_np`
    before API 34 - enable `LOADER_NO_SPAWN` for `__ANDROID__` too.
  - Launchers: "Build for iPhone" button (Windows `main.cpp`, Mac `main_mac.mm`), `--iphone` on
    all three (`main_linux.cpp` too). Windows/Linux launchers were NOT compiled yet (only the Mac
    one; Linux syntax-checked) - build them on this machine first.
- `gtav/src/ios_setup.mm` + `gtav_app.h` `OnFinalizePaths`: the phone-side setup screen. With no
  `--game_data_root`, the app holds its start (the SDK's async `OnFinalizePaths(defaults, resume)`
  hook), finds Disc 1 / Disc 2 / TU by content in its Documents (or takes Files-picker picks), runs
  `RunDataInstall`, then `resume(paths)` with `Documents/data/{game,update,user}`. Reuse the logic
  (`Classify`: Disc 2 = has default.xex + xbox360b.rpf; Disc 1 = has the
  `content/0000000000000000/545408A7/00000002/545408A700000000` package; TU = STFS with
  default.xexp), not the UIKit.
- Package `ios/` folder (`tools/ios/make_shell.sh`): shell app without game code, `interface/`
  (exports/undefs/hooks in the target's format), `libSystem_ios.tbd`, `include/simde`, licenses.
  Release scripts take it as `--ios DIR` (make_release.py, make_release_linux.py) or
  `--ios-shell DIR` (make_release_macos.py) and copy it as-is.
- Memory work that also applies to Android (backend.cpp, `GTAV_MOBILE`): buffer cache budget
  (`gtav_native_buffer_budget_mb`, 96 MB on phones; the cache had no limit), hitch log only past
  50 ms. The low-memory eviction (`gtav_native_low_memory_mb`) is iOS-only (`os_proc_available_
  memory`); an Android equivalent could read `/proc/meminfo` MemAvailable or onTrimMemory.

## Recommended Android design (simpler than iPhone)

Android, unlike iOS, lets an app `dlopen` a library it copied into its own private storage, and
installs APKs without a sideloading tool. So:

1. **The APK is the public release** (GitHub release download, signed with a project key, like the
   desktop packages): SDL/GameActivity Java, `libmain.so` built with `GTAV_GAME_DLL=ON` (no game
   code; the Android equivalent of the iOS shell / desktop `gtav_fast`), librexruntime.so,
   librexgpu-xenos.so, libc++_shared.so. No game code, no shaders, no data.
2. **"Build for Android" on any PC** (`RunAndroid` in install.cpp, mirroring `RunIphone`; button
   next to "Build for iPhone", `--android`) makes one file: `android/gtav-android-pack.zip` =
   `libgtav_game.so` (the player's game code for aarch64 Android) + `spirv/` (Adreno rules) +
   `expected.txt` + a small version/manifest file (which APK build it matches: hash of the
   shipped libmain.so).
3. **The APK's setup screen** (first launch, before SDLActivity starts the game): the player picks
   (or the app finds in Download/) the pack and the three disc files; it unpacks the pack into the
   app's private files dir (`.so` made read-only, 0444), runs `RunDataInstall` into app storage,
   then starts the game. Later app updates keep the data; a new pack is needed only when the
   shell's interface changes (check the manifest hash and say so).

Why not have the PC build a whole APK (as the ipa): Android requires APK Signature Scheme v2+ for
targetSdk 30+, so the installer would need RSA key generation, X.509 and the v2 signing block in
C++ (or bundled Java + apksigner), and every player's APK would carry a different key. The pack
gives the player the same number of steps (copy pack + discs to the phone, press Install).

Stretch (no PC at all, later): run codegen + clang on the phone (ship them as `lib*.so` in the APK
so they are executable from nativeLibraryDir), ~406 files at ~1 GB per clang job. Shaders would
need XenosRecomp + dxc built for linux-arm64. Not for the first version.

## Work list

1. **Shell split on Android.** `GTAV_GAME_DLL=ON` for the Android target (gtav/CMakeLists.txt,
   `gen_interface.py` Linux/ELF mode as on Linux): libmain.so without game code that `dlopen`s
   `<filesDir>/game/libgtav_game.so` by absolute path. Verify first, on the S23:
   - dlopen of a 0444 .so from the app's private files dir works for this targetSdk (it should:
     untrusted_app may mmap-exec app_data_file; only execve is forbidden since API 29), and the
     linker namespace accepts the absolute path.
   - Symbol binding: on Linux gtav_game.so's hooks bind to the exe (`-export_dynamic`). libmain.so
     is loaded RTLD_LOCAL by System.loadLibrary, so link libgtav_game.so with a DT_NEEDED on
     `libmain.so` (and librexruntime.so) so its undefined hooks/bridge/kernel imports resolve
     against them. Keep `-Bsymbolic`, `-z now` (a missing symbol fails at load, not mid-game).
2. **PC side, `RunAndroid` in install.cpp** (any host, like `RunIphone`):
   - Compile flags: those of `tools/android/build_game.sh` for the generated code
     (`--target=aarch64-linux-android30`, `-march=armv8.2-a+lse`, `-fPIC`, plus the freestanding
     set: `-nostdinc++ -nostdlibinc -fno-stack-protector -O2 -std=gnu++23 -fno-exceptions -fno-rtti
     -fno-threadsafe-statics -fno-math-errno`), `-isystem <pkg>/android/include` (simde, as ios/).
     Keep the `gtav_register.cpp` -O0 / no-GlobalISel rule if Android still needs it. The game
     code is freestanding: no NDK headers needed.
   - Link with the package's lld, `-flavor gnu`, `-shared -soname libgtav_game.so -Bsymbolic
     -z now -z relro -z noexecstack`, version script from `android/interface/`, against the
     shipped `android/link/{libmain.so,librexruntime.so}` and tiny stub `libc.so`/`libm.so` we
     generate at release-build time (empty ELF shared objects with the right soname: only the
     DT_NEEDED entries matter; no NDK files shipped). Response file with forward slashes (lld
     tokenizes it GNU-style; see `LinkIphone`).
   - Shaders: Android's rules from `tools/android/make_spirv.sh`: `UboConstants()` (already C++)
     plus `lower_int64.py` (Adreno has no shaderInt64) and whatever `lower_header.py` does for
     shader_common.h - port both to C++ and check every output **byte-identical** to the Python
     pipeline over the full 3,604-shader cache (that is how the iOS port was verified). Plus the
     `ps_D258EBE32001470C_tiles` variant (`kVariants`).
   - Pack: generalise `WriteIpa` into a zip writer and write `gtav-android-pack.zip`.
3. **Package `android/` folder** (like `ios/`, from a new `tools/android/make_shell.sh` run on
   Linux): link inputs, interface lists, simde, licenses; release scripts `--android DIR`. The
   release APK itself is published next to the desktop packages.
4. **App setup screen** (Java Activity started before SDLActivity, native work over JNI):
   - Storage: use the Storage Access Framework (ACTION_OPEN_DOCUMENT for the pack and the three
     disc files, or ACTION_OPEN_DOCUMENT_TREE on Download). Pass the native side
     `/proc/self/fd/<fd>` paths from `ContentResolver.openFileDescriptor` - `OpenFileBlob` reads
     those like files (keep the ParcelFileDescriptors open for the whole install). No
     MANAGE_EXTERNAL_STORAGE (Play/Android dislike it).
   - Native: `RunDataInstall(appStorage, <unpacked>/expected.txt, inputs, ui)` on a worker thread,
     UI callbacks marshalled to the UI thread; Ask() -> AlertDialog; keep the screen on;
     a foreground service if the install should survive leaving the app (~16 GB takes a while).
   - Then hand the data roots to the game (the SDK's `OnFinalizePaths` hook as on iOS, or write
     files/args.txt with them) and start SDLActivity.
   - Offer to delete the disc images/pack afterwards (they are ~16 GB).
5. **README_LINUX / README / README_MACOS**: an "Android" section next to "iPhone" (copy its
   shape: play on the PC a while, Build for Android, copy `gtav-android-pack.zip` + both ISOs + TU
   to the phone's Download folder, install the APK, open it, press Install). Missing shaders:
   same loop as iPhone (phone saves them to shader_cache; copy back, rebuild the pack).
6. **Tests**: full `--android` run on Linux against a real install (expect ~10 min codegen +
   compile + shaders), then on Windows if a Windows box is available; on the phone: fresh install
   of the release APK with no files dir, setup from the ISOs in Download, boot to the open world.

## Gotchas found on the iPhone side

- lld: every package's lld is one multi-format binary; `-flavor <gnu|darwin>` works whatever the
  file is called (checked with names `ld.lld`, `lld-link`, `ld64.lld`).
- Zip: write Unix modes yourself (Windows' zipfile drops them); skip `._*` AppleDouble files.
- The shader cache the PC has is all the phone gets; shaders the phone meets later are saved in
  its shader_cache (`gtav_native_save_missing_shaders`, iOS default on - turn it on for Android:
  `gtav_native_compile` is off there too) for the next PC run.
- `devicectl`-style big log pulls were flaky on iOS; keep phone logs rotating small (16 MB).

## Getting this branch

The iPhone work is on branch `ios-android` (= `origin/android` at d6a2d41 + 20 iOS commits). Since
then `android` gained 10 commits here (render scale, tile fixes, **the GTK setup window for the
Linux launcher**). Merge `ios-android` into `android` before starting. Expected conflicts:
`loader/src/main_linux.cpp` (the Mac added a terminal `--iphone` path; put "Build for iPhone" and
"Build for Android" buttons into the new GTK window as on Windows/Mac, keep `--iphone`/`--android`
for scripts), `gtav/src/native/backend.cpp` (GTAV_MOBILE defaults, buffer budget, texture sample
knob next to the render-scale work), `docs/IOS_PORT.md`/READMEs (text only).
