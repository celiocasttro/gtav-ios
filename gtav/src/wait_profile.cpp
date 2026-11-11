// gtav - where the game's threads wait (--gtav_wait_profile): time spent in the game's wrappers of
// the kernel wait functions, per host thread and per caller (return address), logged every 5 s.
// Finds what gates a frame once no thread is CPU-bound (they take turns waiting on each other).

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include "wait_profile.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#ifdef __APPLE__
#include <pthread.h>
#else
#include <sys/syscall.h>
#endif
#endif

REXCVAR_DEFINE_BOOL(gtav_wait_profile, false, "GTAV",
                    "Log where the game's threads wait (kernel wait wrappers, per thread and caller)");

namespace {

using Clock = std::chrono::steady_clock;

struct Site {
  double ms = 0;
  uint64_t count = 0;
};
struct ThreadTable {
  std::mutex mutex;
  long tid = 0;
  std::string name;
  std::unordered_map<uint64_t, Site> sites;  // wrapper << 32 | caller
};
// Semaphores (--gtav_wait_profile): ms waited per (handle, waiting thread) and releases per
// (handle, releasing thread, caller), to see who wakes whom.
struct HandleStats {
  std::unordered_map<std::string, double> waited_ms;
  std::unordered_map<std::string, uint64_t> releases;
};
std::mutex g_handles_mutex;
std::unordered_map<uint32_t, HandleStats> g_handles;

std::mutex g_tables_mutex;
std::vector<std::shared_ptr<ThreadTable>> g_tables;
Clock::time_point g_window_start = Clock::now();
std::mutex g_report_mutex;

ThreadTable& Table() {
  thread_local std::shared_ptr<ThreadTable> t;
  if (!t) {
    t = std::make_shared<ThreadTable>();
#ifdef _WIN32
    t->tid = long(GetCurrentThreadId());
    PWSTR desc = nullptr;
    if (SUCCEEDED(GetThreadDescription(GetCurrentThread(), &desc)) && desc) {
      char name[64] = {};
      WideCharToMultiByte(CP_UTF8, 0, desc, -1, name, sizeof(name) - 1, nullptr, nullptr);
      LocalFree(desc);
      t->name = name;
    }
    if (t->name.empty()) t->name = fmt::format("t{}", t->tid);
#else
    char name[64] = {};
#ifdef __APPLE__
    uint64_t tid = 0;
    pthread_threadid_np(nullptr, &tid);
    t->tid = long(tid);
    pthread_getname_np(pthread_self(), name, sizeof(name));
#else
    t->tid = syscall(SYS_gettid);
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/self/task/%ld/comm", t->tid);
    if (FILE* f = std::fopen(path, "r")) {
      if (std::fgets(name, sizeof(name), f)) name[std::strcspn(name, "\n")] = 0;
      std::fclose(f);
    }
#endif
    t->name = name;
#endif
    std::lock_guard lock(g_tables_mutex);
    g_tables.push_back(t);
  }
  return *t;
}

void Report() {
  auto now = Clock::now();
  double window = std::chrono::duration<double>(now - g_window_start).count();
  if (window < 5.0) return;
  std::unique_lock rl(g_report_mutex, std::try_to_lock);
  if (!rl.owns_lock()) return;
  g_window_start = now;
  std::vector<std::shared_ptr<ThreadTable>> tables;
  {
    std::lock_guard lock(g_tables_mutex);
    tables = g_tables;
  }
  for (auto& t : tables) {
    std::vector<std::pair<uint64_t, Site>> sites;
    {
      std::lock_guard lock(t->mutex);
      sites.assign(t->sites.begin(), t->sites.end());
      t->sites.clear();
    }
    double total = 0;
    for (auto& s : sites) total += s.second.ms;
    if (total < window * 1000 * 0.02) continue;  // waits under 2% of the time
    std::sort(sites.begin(), sites.end(), [](auto& a, auto& b) { return a.second.ms > b.second.ms; });
    std::string line;
    for (size_t i = 0; i < std::min<size_t>(sites.size(), 5); i++)
      line += fmt::format(" | {:08X} from {:08X}: {:.1f}% x{}", uint32_t(sites[i].first >> 32), uint32_t(sites[i].first),
                          sites[i].second.ms / (window * 10), sites[i].second.count);
    REXLOG_INFO("[waits] {} ({}) waits {:.1f}% of the time{}", t->name, t->tid, total / (window * 10), line);
  }
  std::unordered_map<uint32_t, HandleStats> handles;
  {
    std::lock_guard lock(g_handles_mutex);
    handles.swap(g_handles);
  }
  for (auto& [handle, h] : handles) {
    double total = 0;
    std::string waiters, releasers;
    for (auto& [name, ms] : h.waited_ms) {
      total += ms;
      if (ms > window * 1000 * 0.05) waiters += fmt::format(" {} {:.1f}%", name, ms / (window * 10));
    }
    if (waiters.empty()) continue;
    for (auto& [name, n] : h.releases) releasers += fmt::format(" | {} x{}", name, n);
    REXLOG_INFO("[waits] semaphore {:08X} waited by{}; released by{}", handle, waiters, releasers);
  }
}

uint32_t Be32(const uint8_t* base, uint32_t address) {
  const uint8_t* p = base + address;
  return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

// The return address `depth` frames above the wrapper's caller: each frame's back chain is at
// 0(r1) and the code saves LR at -8 from the caller's frame (mflr r12; __savegprlr).
uint32_t GuestCaller(PPCContext& ctx, uint8_t* base, int depth) {
  uint32_t sp = uint32_t(ctx.r1.u64);
  for (int i = 0; i < depth; i++) {
    if (sp < 0x70000000 || sp >= 0x80000000) return 0;  // guest stacks live at 7xxxxxxx
    sp = Be32(base, sp);
  }
  if (sp < 0x70000000 || sp >= 0x80000000) return 0;
  return Be32(base, sp - 8);
}

template <class F>
void Timed(uint32_t wrapper, PPCContext& ctx, uint8_t* base, F&& call) {
  if (!REXCVAR_GET(gtav_wait_profile)) return call();
  // Two levels up: the wrappers are called through small helpers (semaphore wait 83216200).
  uint32_t caller = GuestCaller(ctx, base, 1);
  if (!caller) caller = uint32_t(ctx.lr);
  auto start = Clock::now();
  call();
  double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
  auto& t = Table();
  {
    std::lock_guard lock(t.mutex);
    auto& s = t.sites[uint64_t(wrapper) << 32 | caller];
    s.ms += ms;
    s.count++;
  }
  Report();
}

}  // namespace

namespace gtav {
void WaitProfileTimed(uint32_t wrapper, PPCContext& ctx, uint8_t* base, void (*call)(PPCContext&, uint8_t*)) {
  Timed(wrapper, ctx, base, [&] { call(ctx, base); });
}
}  // namespace gtav

// RAGE's semaphore helpers: wait (r3 handle, r4 count) and release (r3 handle, r4 count).
REX_EXTERN(__imp__sub_83216200);
REX_HOOK_RAW(sub_83216200) {
  if (!REXCVAR_GET(gtav_wait_profile)) return __imp__sub_83216200(ctx, base);
  uint32_t handle = ctx.r3.u32;
  auto start = Clock::now();
  __imp__sub_83216200(ctx, base);
  double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
  auto& t = Table();
  std::lock_guard lock(g_handles_mutex);
  g_handles[handle].waited_ms[t.name] += ms;
}

REX_EXTERN(__imp__sub_83216298);
REX_HOOK_RAW(sub_83216298) {
  if (REXCVAR_GET(gtav_wait_profile)) {
    auto& t = Table();
    std::string key = fmt::format("{} from {:08X}", t.name, uint32_t(ctx.lr));
    std::lock_guard lock(g_handles_mutex);
    g_handles[ctx.r3.u32].releases[key]++;
  }
  __imp__sub_83216298(ctx, base);
}

#define GTAV_WAIT_HOOK(addr)                                                  \
  REX_EXTERN(__imp__sub_##addr);                                              \
  REX_HOOK_RAW(sub_##addr) {                                                  \
    Timed(0x##addr, ctx, base, [&] { __imp__sub_##addr(ctx, base); });         \
  }

// Game functions that call NtWaitForSingleObjectEx / KeWaitForSingleObject /
// KeWaitForMultipleObjects (found in the generated code).
GTAV_WAIT_HOOK(837972B8)
GTAV_WAIT_HOOK(8378F5D0)  // SleepEx (the only caller of KeDelayExecutionThread)
GTAV_WAIT_HOOK(83798C98)
GTAV_WAIT_HOOK(83798C00)
GTAV_WAIT_HOOK(83798B68)
GTAV_WAIT_HOOK(83791358)
GTAV_WAIT_HOOK(8378ED80)
GTAV_WAIT_HOOK(837C9000)
GTAV_WAIT_HOOK(837C52A8)
GTAV_WAIT_HOOK(83798D30)
GTAV_WAIT_HOOK(83797E78)
GTAV_WAIT_HOOK(83790B38)
GTAV_WAIT_HOOK(83790900)
GTAV_WAIT_HOOK(83790830)
GTAV_WAIT_HOOK(83790750)

// D3D functions that spin on the GPU fence (they loop on the spin helper 83807DC8, which reads
// the completed-fence value at device+11024): time the game spends waiting for the GPU.
GTAV_WAIT_HOOK(8380C8F0)
GTAV_WAIT_HOOK(8380C988)
GTAV_WAIT_HOOK(838204D8)
GTAV_WAIT_HOOK(83820A90)

// --gtav_wait_profile: a timeline of the audio engine's passes (sub_831CD850), sound command
// submissions (sub_831B83A0) and the waits for them (sub_831B83F0), logged once (400 events).
namespace {
struct TimelineEvent {
  double ms;
  const char* what;
  std::string thread;
};
std::mutex g_timeline_mutex;
std::vector<TimelineEvent> g_timeline;
Clock::time_point g_timeline_start;
int g_timeline_state = 0;  // 0 waiting to start, 1 recording, 2 logged

void Event(const char* what) {
  if (!REXCVAR_GET(gtav_wait_profile)) return;
  auto now = Clock::now();
  std::lock_guard lock(g_timeline_mutex);
  if (g_timeline_state == 0) {
    static auto first = now;
    if (now - first < std::chrono::seconds(40)) return;  // well into the game
    g_timeline_state = 1;
    g_timeline_start = now;
  }
  if (g_timeline_state != 1) return;
  g_timeline.push_back({std::chrono::duration<double, std::milli>(now - g_timeline_start).count(), what, Table().name});
  if (g_timeline.size() < 400) return;
  std::string out;
  for (auto& e : g_timeline) out += fmt::format("\n  {:9.3f} {:16} {}", e.ms, e.thread, e.what);
  REXLOG_INFO("[waits] timeline:{}", out);
  g_timeline_state = 2;
}
}  // namespace

REX_EXTERN(__imp__sub_831CD850);
REX_HOOK_RAW(sub_831CD850) {
  Event("pass start");
  __imp__sub_831CD850(ctx, base);
  Event("pass end");
}

REX_EXTERN(__imp__sub_831B83A0);
REX_HOOK_RAW(sub_831B83A0) {
  Event("submit");
  __imp__sub_831B83A0(ctx, base);
}

REX_EXTERN(__imp__sub_831B83F0);
REX_HOOK_RAW(sub_831B83F0) {
  Event("wait begin");
  __imp__sub_831B83F0(ctx, base);
  Event("wait end");
}
