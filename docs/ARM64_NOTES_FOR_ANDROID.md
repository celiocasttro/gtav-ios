# arm64 notes (macOS M1 port) for the Android port — 2026-10-30

Written at the end of the Mac performance session for whoever starts Android (on the Linux
machine). Everything here was measured on an 8 GB M1 MacBook (MoltenVK 1.4.1, native renderer
`GTAV_NATIVE=on`). Read with MAC_THEN_ANDROID_PLAN.md section 4 and the HANDOFF_MAC_* files.

## 1. State of the Mac port

- Game builds and runs on arm64 macOS: prologue, cutscenes, open world, audio on the built-in
  speakers. Native renderer correct by eye (sky slabs, pipeline flashes, open-world one-frame
  slabs fixed; see HANDOFF_MAC_2026-10-30_openworld.md). Open: water height drifts, rare leftover
  slabs after >100 ms stalls.
- Speed (open world, idle autosave at Franklin's house, headless, uncapped): ~45-55 fps with
  2,000-3,800 draws, ~60+ fps in lighter views. Not yet 60 everywhere. Per frame: GPU 17-20 ms,
  render thread recording 12-16 ms, and in the heaviest views the game's own threads.
- Launch: `GTAV_IGNORE_SETTINGS=1 GTAV_NATIVE=on ./play_mac.sh <log> --gtav_native_hidden=true`
  (`GTAV_IGNORE_SETTINGS` = benchmark without gtav_settings.ini; menu_set no longer saves then).

## 2. arm64 code that Android reuses as is

- `gtav/src/native/byteswap_simd.h`: 16-byte swaps with NEON (constants, indices).
- `gtav/src/native/safe_read.h`: fault-free reads (`process_vm_readv` path on Linux/Android).
- Vertex conversion (`ConvertElement` in backend.cpp): format dispatched once per element, swaps
  as 16/32-bit loads + `__builtin_bswap*` (was a per-vertex switch with byte copies).
- CMake: `gtav_register.cpp` at -O0 without GlobalISel/PCH (clang's arm64 backend took hours on it;
  the NDK's clang is the same backend). Prebuilt native shaders (`GTAV_PREBUILT_NATIVE_SHADERS`)
  when dxc cannot run on the build host.
- Page size: Apple is 16 KB, Android arm64 is usually 4 KB (some new devices 16 KB): nothing in the
  native renderer assumes 4 KB host pages; the SDK's physical heap uses the host page size.
- Thread QoS (macOS only, SDK threading_posix.cpp + renderer/submit threads): on Android the
  equivalent is thread priority / `setpriority`, and big.LITTLE placement matters even more.

- **CPU target:** the Mac game build was configured with `-march=armv8-a` (SDK preset default),
  which overrides Apple clang's `apple-m1` default: plain ARMv8.0, no LSE atomics. Rebuilt with
  `-DCMAKE_C_FLAGS=-mcpu=apple-m1 -DCMAKE_CXX_FLAGS=-mcpu=apple-m1` (2026-10-30): loading ~3%
  faster in both A/B pairs, fps slightly up (`tools/native/bin_ab.sh`). The SDK runtime the same way (mac preset now `-mcpu=apple-m1`): loading another ~11% faster (2.6-2.7 s in both pairs, `tools/native/lib_ab.sh`). Android: build for at least
  `-march=armv8.2-a` (LSE) or the phone's `-mcpu`, not the armv8-a preset.

## 3. Tile GPUs: what costs and what helped (applies to Adreno / Mali too)

**A render pass costs ~50 us on the M1 no matter how small** (tools/native/metal_pass_bench.mm,
native Metal: 150 passes of one 64x64 draw = 8.3 ms dependent, 7.2 ms independent). GTA V issues
~175-190 passes a frame, so ~9 ms of the GPU frame is pass overhead. On phones expect worse.
Fewer passes is the lever; bandwidth is not (skipping loads of fully overwritten resolve targets
changed nothing).

Upper bound measured: skipping all colour resolve copies (wrong image) took the GPU frame from
18.3 to 13.2 ms (`--gtav_native_skip_resolves=1`, benchmark only; `=200000` skips only the
full-screen ones: half of that).

Kept (A/B in the same run, see section 6):
| change | cvar (default) | GPU per frame |
|---|---|---|
| small resolve writebacks dispatched at frame end, not between passes | gtav_native_defer_writebacks (true) | -0.3..0.5 ms |
| resolve "clear source" applied in the target's next pass, not a pass of its own | gtav_native_defer_clears (true) | -0.2..0.4 ms |
| plume: no TOP/BOTTOM_OF_PIPE next to real stages (MoltenVK maps them to every stage); render-target transitions wait at the fragment stage only | gtav_native_exact_barriers, gtav_native_fragment_barriers (true) | -0.3 ms |
| whole-destination resolve copies use LOAD_OP_DONT_CARE | gtav_native_resolve_discard (true) | ~0 (kept, cheaper on bandwidth) |

| colour clears as draws in the draws' pipeline layout, not vkCmdClearAttachments (MoltenVK restarted the Metal render pass at each clear: ~34 extra encoders a frame) | gtav_native_draw_clears (true) | -2.0..2.2 ms |
| GTA V's EDRAM depth-restore strips skipped (25 passes into targets nothing reads) | gtav_native_skip_depth_restore (true) | -0.8 ms |

Finding encoders: `tools/native/mst_labels.py` joins MoltenVK's encoder labels ("vkCmdBeginRenderPass",
"Metal renderpass restart", "vkCmdResolveImage (... stage)", "Post-render-pass occlusion query
accumulation", ...) from a Metal System Trace with the GPU intervals: count, busy and the idle gap
before each kind per frame. A clear at the start of a pass cost a whole encoder; a clear as a draw
costs nothing extra. Other MoltenVK extras per frame: vkCmdResolveImage = 2-3 encoders (the
reflection cubemap's 6 MSAA faces), occlusion queries = an accumulation compute encoder each.

Tried and not kept:
- Render targets per frame in flight (frames overlap on the GPU): `gtav_native_slot_targets`
  (false) — no change, doubles target memory.
- In-pass resolves (input attachment + image store inside the source pass, pass kept open):
  **now on by default on macOS** (2026-10-30 evening; `GTAV_NATIVE_FETCH_RESOLVE=0` turns them off).
  Storage use only for small destinations (`gtav_native_storage_max_texels`, 640x360) because
  STORAGE costs the texture its lossless compression (~1 ms). The black phone screen / blown-out
  sky / black top tile came from passes with **more than one draw** before the resolve: there the
  framebuffer read returned black although the draws had rendered (a green `clearColor` just before
  the read was seen, so fetch itself works; no pass restart in between). Not understood (MoltenVK /
  Apple raster order?). `gtav_native_fetch_max_draws` (default 1) keeps in-pass resolves to
  one-draw passes: ~43 of ~46 a frame (the post-processing chains), correct by eye in five scenes.
  Live A/B in a heavy scene: 35.9 -> 37.9 fps, GPU -1.5 ms. Diagnostics: `gtav_native_fetch_range`
  (lo:hi of the frame's candidates, bisecting), `gtav_native_fetch_debug` (1 magenta, 2 source+red,
  3 alpha 1, 4 green clear before the read), `gtav_native_seqdiag=1` with GTAV_NATIVE_PASS_STATS
  (the next frame's command sequence with pass starts and each in-pass resolve's attachments).
- Compute resolves (earlier session): fewer passes but STORAGE made it slower.

## 4. CPU (render thread) on arm64

Profile (`sample`) of the render thread in heavy views, after this session's changes: vertex
conversion of per-frame data ~11%, texture/buffer hashing ~15% (no write watch on macOS),
rect-list expansion ~10%, fence-copy pushes ~11% (memory pressure: see 5).
- Rect lists without geometry shaders are expanded on the CPU (Mali has no geometry shaders
  either). The tiled-lighting rects: ~84 draws, ~1.6 MB of vertices a frame. New cache by draw
  site (`RectDraw`/`RectCache`): ~49 a frame come from the cache, ~35 change every frame.
- Write watch: Linux uses userfaultfd + PAGEMAP_SCAN; macOS has none, Android normally denies
  userfaultfd (SELinux). The SDK's mprotect watch (`EnablePhysicalMemoryAccessCallbacks`) was
  measured on Linux as too slow under its global lock. An own mprotect + SIGSEGV watch must run
  before the SDK's handler (its "stale protection" recovery would swallow our faults) and must
  cover host syscalls writing guest memory (HostPathFile `pread` -> EFAULT); not done yet.
- Per-draw timing (`prof.Mark`, 6 clock reads a draw) now runs on every 8th frame only.
- Hash worker threads (`gtav_native_hash_threads`, now 3 by default on macOS): the frame's small
  textures/buffers are hashed on workers before recording: record -2.4 ms a frame (live A/B).
- Plume ends the render pass for every occlusion query begin/end (`beginOcclusionQuery`): a few
  invisible extra passes a frame (~6 queries). Keeping queries inside the pass needs the query to
  end before the pass does (Vulkan rule); not done.
- The game waits on the renderer at GPU fences (`gtav_native_fence_wait`, macOS): ~7.7 ms of
  waiting per frame in heavy views, i.e. the game and the renderer partly serialize. Waiting per
  draw instead of per frame (`gtav_native_fence_wait_draw`, off) let the command processor reach
  the fences of the frame still being built, which are copied: ~3x the copied bytes and no gain
  (measured while the Mac was swapping hard; worth re-measuring after a reboot).

## 5. Memory (the 8 GB Mac swapped; phones have less)

- **The SDK's emulated Vulkan renderer still initializes under gpu_skip_rendering.** Its shared
  memory was a full 512 MB device-local buffer (no sparse binding on MoltenVK; likely none on
  phones). Now 1 MB under skip rendering (`vulkan_skip_rendering_small_shared_memory`, SDK patch):
  GPU private memory 1.0 GB -> 0.5 GB, no errors. The rest of that renderer (EDRAM, caches, its
  pipelines) is still created — next candidate.
- Frame packets are ~18 MB at ~3,800 draws (`[native] packets per 300 frames` line); arenas keep
  their capacity, but new packets re-grow (log: "packet arenas grew N times (M MB moved)"): each
  growth copies tens of MB (a hitch). Next: size new packets from the largest seen.
- Footprint ~2.8 GB (footprint/vmmap): 0.5 GB private GPU, 0.5 GB shared GPU, ~0.6 GB guest,
  ~1.1 GB malloc (pipelines: ~4,500 MoltenVK pipelines warmed at boot, SPIR-V preloads, packets).
- With 23 GB of swap in use the game paged in ~2,000 pages a second: stalls in plain memcpy. On a
  phone, keep the warm-up pipeline set and caches bounded.

## 6. Measuring

- `--gtav_native_ab=<cvar>=<a>|<b>` (live via `echo gtav_native_ab=... > build/gtav/logs/menu_set`):
  flips the cvar every 60 presented frames and logs `[native] A/B ... fps, frame, record, GPU` per
  setting every 1,200 frames. Both settings see the same scenes; separate runs did not (the idle
  autosave scene varies from 1,000 to 3,800 draws between runs).
- GPU frame time from Vulkan timestamps around the main command list: `GPU frame N ms, idle
  before M ms` in the `avg acquire` line; `submit thread busy` = MoltenVK encode + present.
- `tools/native/fps_from_log.py <log>`: fps per 300 frames with the record breakdown.
- Metal System Trace (xctrace, <= 3 s; delete /private/var/folders/*/*/T/instruments*.ktrace).
- Viz-query begin/end lines were logged at info per query (~6 per frame on the command processor
  thread): now debug.

## 7. Merge notes (2026-10-30, onto origin/windows-native with the clean-room release work)

- The sun-tile fix is now a runtime variant (`ps_D258EBE32001470C_tiles.spv`, shader_compiler.cpp
  kVariants) made with XenosRecomp + dxc on the player's machine. The Mac has no dxc, so the
  variant was never made and the base shader's draws were skipped: **all deferred lighting missing
  (flat grey world)**. Fixed: `TileOverride` only applies once the variant file exists; without it
  the game's own shader draws (blocky shadow tiles on cars, the old look). Android: no dxc on the
  phone either — generate the variant at install time on a desktop, or port the variant to a
  SPIR-V edit.
- The SDK's own window starts fullscreen (`fullscreen=true`) and stays black under the native
  renderer: headless/benchmark runs need `--fullscreen=false` (it covered the owner's screen).

## 8. SDK / plume patches

`rexglue-sdk-gtav-linux.patch` = origin's patch (with the Windows release work) + the Mac delta
(SDL audio stereo fold, thread QoS, viz-query log level, small shared memory). `plume-gtav.patch`
adds `RenderBarrierStage::FRAGMENT`, `RenderFramebufferDesc::colorDiscard`, `g_exactBarrierStages`.
Apply both the usual way (MAC_THEN_ANDROID_PLAN.md section 1).
