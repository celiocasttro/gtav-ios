// gtav - the game blocking on a GPU fence (native renderer, --gpu_skip_rendering).
//
// Every frame the update thread waits until the GPU has finished an earlier frame (sub_835833D8
// -> sub_832BA460 -> D3D 8380CEF8, which spins on the completed-fence value at device+11024).
// The command processor runs --gpu_skip_rendering_lag_ms behind the CPU's kicks (so draw data
// the game fills after a kick is complete when it is read), which made every such wait take the
// whole lag: ~6 ms of a ~20 ms frame. While a game thread is blocked here the SDK command
// processor lets kicks through one at a time instead (rex_skip_rendering_gpu_waiters).

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <chrono>

#include <atomic>

#include "wait_profile.h"

// SDK (librexgpu): wakes the command processor from its idle sleep.
extern "C" void rex_skip_rendering_wake();

namespace {
std::atomic<int> g_gpu_waiters{0};
}

#ifdef _WIN32
#define GTAV_HOOK_EXPORT __declspec(dllexport)
#else
#define GTAV_HOOK_EXPORT __attribute__((visibility("default")))  // exported by the link options
#endif
extern "C" GTAV_HOOK_EXPORT int rex_skip_rendering_gpu_waiters() { return g_gpu_waiters.load(std::memory_order_acquire); }

REXCVAR_DECLARE(bool, gtav_wait_profile);

namespace {
uint32_t Be32(const uint8_t* base, uint32_t address) {
  const uint8_t* p = base + address;
  return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
}
// --gtav_wait_profile: how far the fence waited for is behind the newest one the CPU issued and
// ahead of the one the GPU completed (in fence steps of 2), and how long the waits take.
struct {
  uint64_t waits = 0, behind_newest = 0, ahead_of_done = 0, newest = 0;
  double ms = 0;
} g_fence_stats;
}  // namespace

// D3D: block until the GPU passes a fence (r3 device, r4 fence value).
REX_EXTERN(__imp__sub_8380CEF8);
REX_HOOK_RAW(sub_8380CEF8) {
  g_gpu_waiters.fetch_add(1, std::memory_order_acq_rel);
  rex_skip_rendering_wake();  // it may be sleeping up to 1 ms before it lets kicks through
  const bool stats = REXCVAR_GET(gtav_wait_profile);
  uint32_t device = ctx.r3.u32, fence = ctx.r4.u32;
  auto start = std::chrono::steady_clock::now();
  if (stats && device && fence) {
    uint32_t next = Be32(base, device + 11036), done = Be32(base, Be32(base, device + 11024));
    g_fence_stats.behind_newest += (next - fence) / 2;
    g_fence_stats.ahead_of_done += int32_t(fence - done) > 0 ? (fence - done) / 2 : 0;
    g_fence_stats.newest += fence == next;
  }
  gtav::WaitProfileTimed(0x8380CEF8, ctx, base, [](PPCContext& c, uint8_t* b) { __imp__sub_8380CEF8(c, b); });
  g_gpu_waiters.fetch_sub(1, std::memory_order_acq_rel);
  if (stats && device && fence) {
    g_fence_stats.ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (++g_fence_stats.waits == 500) {
      auto& st = g_fence_stats;
      REXLOG_INFO("[waits] GPU fence waits: avg {:.2f} ms, target {:.1f} fences behind the newest issued "
                  "({:.0f}% the newest), {:.1f} ahead of the completed one",
                  st.ms / st.waits, double(st.behind_newest) / st.waits, 100.0 * st.newest / st.waits,
                  double(st.ahead_of_done) / st.waits);
      st = {};
    }
  }
}
