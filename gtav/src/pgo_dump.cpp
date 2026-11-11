// gtav - profile-guided optimization (GTAV_PGO=GENERATE builds only): the instrumented game
// writes its profile GTAV_PGO_DUMP_SECS seconds after start (test runs are killed, so the
// runtime's own write at exit never happens). LLVM_PROFILE_FILE names the file.

#include <rex/logging.h>

#include <chrono>
#include <cstdlib>
#include <thread>

extern "C" int __llvm_profile_write_file(void);

namespace {
const bool g_started = [] {
  const char* secs = std::getenv("GTAV_PGO_DUMP_SECS");
  if (!secs) return false;
  int s = std::atoi(secs);
  std::thread([s] {
    std::this_thread::sleep_for(std::chrono::seconds(s));
    int result = __llvm_profile_write_file();
    REXLOG_INFO("[pgo] profile written ({})", result);
  }).detach();
  return true;
}();
}  // namespace
