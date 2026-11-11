# iOS (iPhone) port — dev build

Started 2026-10-31 on the macOS arm64 work (main c3d51dd); rebased onto the Android branch (branch ios-android). Target: the owner's iPhone 13 mini
(A15, 4 GB RAM, iOS 26.4.2), Xcode 27.1 / iOS SDK 27.1. **Dev build only: the app contains the
recompiled game code and must never be published** (see RELEASE_CLEANROOM.md for the rule and
"Release plan" below for how players could get it).

## Build

```sh
# 1. SDK runtime for iOS (librexruntime.dylib, librexgpu-xenos.dylib; no codegen tool, no loader)
cd rexglue-sdk
cmake -S . -B out/build/ios-arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_FLAGS=-mcpu=apple-a15 -DCMAKE_CXX_FLAGS=-mcpu=apple-a15 -DCMAKE_CXX_STANDARD=23 \
  -DREXGLUE_ENABLE_TRACY=OFF -DCMAKE_INSTALL_PREFIX=$PWD/out/install/ios-arm64
ninja -C out/build/ios-arm64 && cmake --install out/build/ios-arm64
# the install registers the package in ~/.cmake/packages/rexglue: delete that entry, or a Mac
# configure could pick up the iOS SDK.

# 2. Game (monolithic, generated code from gtav/generated as on the Mac; ~5 min -j4)
cd ../gtav
cmake -S . -B ../build/ios -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_FLAGS=-mcpu=apple-a15 -DCMAKE_CXX_FLAGS=-mcpu=apple-a15 \
  -DCMAKE_PREFIX_PATH=<repo>/rexglue-sdk/out/install/ios-arm64 \
  -DCMAKE_FIND_ROOT_PATH=<repo>/rexglue-sdk/out/install/ios-arm64
# (the renderer's own shaders are compiled with the Mac's dxc-macos; do not use prebuilt headers:
#  old ones predate the vertex-buffer menu/overlay shader and draw nothing)
ninja -C ../build/ios -j4

# 3. Game shaders for iOS (constants in uniform buffers, as on Android) from a shader cache
#    made by a Mac install (the clean-room test install has all 3,604):
TOOLS=/Volumes/Untitled/gtav-cleanroom-test/tools CACHE=/Volumes/Untitled/gtav-cleanroom-test/data/shader_cache \
  tools/ios/make_spirv.sh 4          # -> build/ios/spirv

# 4. App bundle, signing, install, args (+ shaders) (needs a profile for com.nakas.gtav, below)
tools/ios/deploy.sh [--spirv]        # = make_app.sh --install + copies to Documents
tools/ios/launch_when_unlocked.sh build/ios/run.log   # console log; game log: Documents/logs/gtav.log
```

Patches: rexglue-sdk-gtav-linux.patch, plume-gtav.patch, sdl3-gtav.patch (rexglue-sdk/thirdparty/sdl3)
and ffmpeg-gtav.patch (rexglue-sdk/thirdparty/FFmpeg), applied as on the other platforms. Branch
ios-android = origin/android + the iOS commits; phone paths shared with Android are `GTAV_MOBILE`
in backend.cpp (UBO constants, load-op clears, texture recheck interval) and the letterbox in
renderer.cpp.

Provisioning profile: minted once with a throwaway Xcode project (xcodegen) carrying bundle id
`com.nakas.gtav`, team FBCD7748P3 and the two kernel entitlements, built with
`xcodebuild -target <t> -sdk iphoneos -allowProvisioningUpdates -allowProvisioningDeviceRegistration`.
make_app.sh signs with the profile's own entitlements (application-identifier etc. included).

MoltenVK: the prebuilt ios-arm64 dynamic framework from the Skate 3 iOS port (1.4.2,
`MOLTENVK_FW`), copied into the bundle root as libMoltenVK.dylib. The SDK and plume open it by
its full path next to the executable (iOS has no Vulkan loader; a bare dlopen name does not
search the bundle).

## Bundle layout

`GTAV.app/`: gtav (exe, @executable_path rpath), librexruntime.dylib, librexgpu-xenos.dylib,
libMoltenVK.dylib (each signed), Info.plist (landscape, fullscreen, UIFileSharingEnabled,
UILaunchScreen, game controllers), embedded.mobileprovision.

## Data on the phone (app Documents, visible in Finder / Files)

```
Documents/args.txt          launch arguments (tools/ios/args.txt), relative to Documents
Documents/game/disc2_files  game data root        (7.7 GB)
Documents/game/update       TU27 update root      (54 MB)
Documents/userdata          user data incl. the installed Disc 1 content (7.6 GB) and saves
Documents/spirv             game shaders, translated on the Mac (build/gtav/spirv, 75 MB)
Documents/logs, shader_cache, gtav_settings.ini   written by the game
```

Copy with `xcrun devicectl device copy to --device <udid> --domain-type appDataContainer
--domain-identifier com.nakas.gtav --source <dir> --destination Documents/<dir>` (~19 MB/s USB;
works while the phone is locked; launching does not). The phone needs ~16 GB free.

## What changed for iOS

SDK (rexglue-sdk, uncommitted on top of the SDK patch):
- `REX_PLATFORM_IOS` (TARGET_OS_IPHONE; REX_PLATFORM_MAC stays set), `REX_PLATFORM` ios-arm64
  (own out/ dir), no codegen tool / Vulkan loader / MoltenVK source build on iOS.
- memory_posix: guest memory backed by an unlinked sparse file in TMPDIR (the sandbox denies
  shm_open), no MAP_JIT, vm_region_64 (mach_vm.h is unsupported in the iOS SDK).
- windowed_app_main_sdl: main() -> SDL_RunApp (UIKit owns the main thread); arguments from
  Documents/args.txt; cwd = Documents. SDL video driver "uikit".
- vulkan_instance: libMoltenVK.dylib next to the executable first.
- Touch gamepad: the Android port's gtav/src/native/touch_controls.cpp (on by default on iOS).

Game / plume:
- plume_apple.mm: UIKit branch (UIWindow size x nativeScale, vendor APPLE, no IOKit).
- plume_vulkan.cpp: volkInitializeCustom with the bundled MoltenVK.
- renderer.cpp: reuses the SDK's (only) window, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER.
- paths.h: ExeDir() = $HOME/Documents (the bundle is read-only).
- shader_compiler.cpp: no std::system on iOS, gtav_native_compile defaults off: every shader
  must come precompiled from the Mac (Documents/spirv).
- safe_read.h: vm_read_overwrite.
- gtav/CMakeLists.txt: GTAV_IOS, imported host rex::rexglue for the codegen rule, no tools/ staging.

## Performance and memory (perf run 2026-10-31, iPhone 13 mini)

Measured with the game's own log lines (`memory at frame`, `record ms per frame`, `GPU frame`,
`caches:`), live `menu_set` cvars and `--gtav_native_pass_gpu_times`:
- **Memory was the problem.** Two open-world runs were killed by jetsam after ~6 min (footprint
  2.2 GB of a ~2.35 GB limit with increased-memory-limit; Metal 1.4 GB in 256 MB steps). Fixed:
  plume gives images their own allocations and uses 32 MB VMA blocks on iOS (MoltenVK made each
  256 MB block holding a buffer a 256 MB MTLBuffer, on top of every image's own MTLTexture); the
  vertex/index buffer cache has a budget (`gtav_native_buffer_budget_mb`, 96 MB on phones; it had
  none: 18,000 buffers in minutes); texture budget 192 MB (args.txt); below 600 MB headroom
  (`gtav_native_low_memory_mb`) both caches shrink to half. After: an hour of play, footprint
  1.5-1.7 GB, Metal ~0.9 GB, malloc ~0.85 GB.
- Memory pressure is also what made the CPU slow: guest memory is a file mapping, iOS evicts its
  pages, and the texture/buffer hash checks fault them back in (texture checks 15-117 ms a frame
  under pressure, 1-4 ms after the fix).
- GPU: ~28 ms a frame in the city (30 fps cap holds), 34-38 ms in the busiest scenes. Biggest
  passes: the 1280x720 G-buffer (4 colour + depth) ~10 ms, a 1-target 720p pass with depth ~5 ms,
  the MSAA 160x720 pass ~3 ms; ~160-190 render passes a frame. No VK_KHR_fragment_shading_rate on
  MoltenVK (coarse shading unavailable); a render scale below 720p needs every shader changed.
- Tools: `gtav_native_texture_sample_frames` (large-texture spot checks every N frames, A/B knob),
  hitch log only past 50 ms on phones, logs rotate at 16 MB (`devicectl copy from` stalls on files
  past ~20 MB; when the phone's file service wedges, replug the cable).

## Release (built and tested on the Mac 2026-10-31/11-01; the ipa not yet sideloaded)

**Any PC builds the ipa** (2026-11-01): the iPhone step is one code path in install.cpp for
Windows, Linux and macOS ("Build for iPhone" in the Windows / Mac setup windows, `--iphone` on all
three). Each package's `ios/` folder (tools/ios/make_shell.sh, built on a Mac, copied as-is by
make_release.py / make_release_linux.py `--ios DIR` and make_release_macos.py `--ios-shell DIR`)
holds the shell app, `interface/` (Mac-format exports/undefs/hooks), `libSystem_ios.tbd`,
`include/simde` and the MoltenVK + simde licenses. The compile uses the package's clang with
`--target=arm64-apple-ios17.0`; the link uses the package's lld under whatever name it ships
(`lld-link.exe`, `ld.lld`, `ld64.lld`) with `-flavor darwin` (checked: all three names link an
iOS dylib); the response file uses forward slashes (lld tokenizes it GNU-style); GTAV.ipa is a
stored zip written by install.cpp WriteIpa with Unix modes (no ditto). Windows / Linux are not
compiled or run yet (only syntax-checked on the Mac).

Constraint as for every release: nothing Rockstar-derived is published, and iOS can neither
compile code nor translate shaders, so the player's **Mac** builds the phone's game library and
shaders from its installed game (loader/src/install.cpp `RunIphone`):
- Package: `GTAV.app/Contents/Resources/gtav/ios/GTAV.app` (the iOS shell from
  `tools/ios/make_shell.sh`: gtav built with `GTAV_GAME_DLL=ON`, SDK runtime, MoltenVK, Info.plist;
  unsigned, no game code) and `tools/game_dll/libSystem_ios.tbd`. Release build:
  `IOS_SHELL=1 tools/release/build_release_macos.sh ...` (iOS SDK + shell for A14 with
  -ffile-prefix-map; make_release_macos.py `--ios-shell` checks the iOS binaries) - not yet run.
- Player: "Build for iPhone" in the setup window (Option at launch) or `GTAV --iphone [--home D]`:
  codegen + reference check as for the install, compile for `arm64-apple-ios17.0 -mcpu=apple-a14`,
  ld64.lld against libSystem_ios.tbd and the shell's librexruntime.dylib, shaders from
  `data/shader_cache` (+ `iphone/shader_cache` copied back from the phone) through XenosRecomp, the
  UBO rewrite (tools/android/ubo_constants.py ported to C++; its 3,653 outputs are byte-identical
  to make_spirv.sh) and dxc, then `iphone/GTAV.ipa` (unsigned, for Sideloadly / AltStore) and
  everything else the phone needs inside the app (below). A second run with a current app only
  translates new shaders and repacks the ipa (README_MACOS.txt "iPhone").
- **On-phone game data install** (as the Skate 3 iOS app's ISO wizard): the ipa carries no game
  files; it holds the game library, `spirv/`, `expected.txt` and `args.txt` (the SDK reads the
  bundle's args.txt, then Documents/args.txt). With no `--game_data_root`, GtavApp::OnFinalizePaths
  (gtav_app.h) holds the start and shows the UIKit setup screen (gtav/src/ios_setup.mm): it finds
  Disc 1 / Disc 2 ISOs (or extracted folders) and the TU27 STFS file in Documents (or Inbox) by
  content, or takes picks from the Files picker (security-scoped, e.g. a USB drive), then runs the
  installer's copy stages (install.cpp `RunDataInstall`: Disc 2 + TU into Documents/data/{game,
  update}, the Disc 1 install partitions into data/user, every file hashed against expected.txt,
  resumable) and resumes the boot with those roots. Afterwards it offers to delete disc images from
  Documents. install.cpp + sources.cpp compile into the iOS exe (`LOADER_NO_SPAWN`: no process
  starts there). A dev build's Documents/args.txt with data roots skips all this.
- On the phone, shaders without SPIR-V are skipped and saved to Documents/shader_cache
  (`gtav_native_save_missing_shaders`) for the next Mac run.
- Test (2026-10-31, M1 8 GB): shipped v0.1.0-macos package + iOS shell, game folder linked to the
  clean-room install: 9 min, 811/811 gen hashes, 406 files in 271 s, 3,604 shaders, 0 failures.
- The release layout (shell + gtav_game.dylib from build_game_dll.py --ios) boots on the phone
  (dyld binds the 2930 hooks, Vulkan up); its first run ended with signal 9 after ~10 min, cause
  not read yet (the phone's file service had wedged).

Open:
- Free Apple IDs cannot get `increased-memory-limit` / `extended-virtual-addressing`: measure the
  limit without them (sign with fewer entitlements: footprint + available in `memory at frame`)
  against the 1.5-1.7 GB footprint, and whether the guest address space reserves without EVA.
- Sideload the packaged ipa end to end (Sideloadly), and a fresh Documents (logs/ created?).
- Minimum device: A14 (iPhone 12) is the build target; only the A15 was tested.

## Release plan (original draft, 2026-10-31)

Built and checked on the Mac (2026-10-31, not yet run on the phone):
- Shell: `cmake ... -DGTAV_GAME_DLL=ON -B build/ios-shell` -> gtav_fast (3 MB, no game code; the
  stub gtav_game.dylib is built for iOS via GTAV_STUB_SDK=iphoneos in gen_interface.py).
- Game library: `build_game_dll.py <llvm-bin> <copy of generated> build/ios-shell/game_interface
  <out> --ios --runtime <ios librexruntime.dylib>` with the release's bundled LLVM 19.1.7
  (clang++ / ld64.lld links to clang-19 / lld): 406 TUs in 286 s -j4, 115 MB, platform iOS, links
  against tools/game_dll/libSystem_ios.tbd (no Xcode SDK needed).
- Package: `GTAV_GAME_DYLIB=<out>/gtav_game.dylib tools/ios/make_app.sh`.

Constraint: like the other releases, nothing Rockstar-derived may be published, and iOS can
neither compile code nor load unsigned code. So the player's **Mac** does the building, as the
macOS clean-room GTAV.app already does for the Mac:

1. Publish an **iOS shell** (no game code): gtav exe built with `GTAV_GAME_DLL=ON`, the SDK
   runtime, the GPU plugin, MoltenVK, Info.plist. Same split as the Mac release (game_bridge.cpp,
   gen_interface.py exports list). Taint scan as for the other packages.
2. The macOS GTAV.app setup window gets an "iPhone" step: from the player's discs + TU27 (already
   copied and hash-checked for the Mac install) it cross-compiles the same generated code for
   `arm64-apple-ios17` with the bundled LLVM 19.1.7 (`-mcpu=apple-a14`), links
   `gtav_game.dylib` with ld64.lld against .tbd stubs (as tools/game_dll/libSystem.tbd does for
   macOS; no Xcode needed to build), translates every shader on the Mac (it has dxc) into
   spirv/, and drops the dylib into the shell.
3. Signing + install with the player's own Apple ID. Options: (a) Xcode installed: the tool runs
   `devicectl` and signs with the player's development certificate; (b) no Xcode: the tool
   writes an unsigned .ipa for Sideloadly / AltStore, which re-sign nested dylibs.
4. Game data: the tool pushes disc2_files, update, the installed Disc 1 content and spirv to the
   app's Documents over USB (devicectl), or the player drags them in Finder (file sharing).

Open questions to measure on the dev build first:
- Free Apple IDs cannot get `increased-memory-limit` / `extended-virtual-addressing`. A14+ gets a
  large address space without the second (Skate 3 finding); the first matters if the footprint is
  above the ~2 GB default jetsam limit on 4 GB phones. Measure the footprint here.
- 7-day re-signing for free accounts (AltStore refreshes it).
- Minimum device: A12+ (Metal/MoltenVK features, 3-4 GB RAM) — to be measured.
