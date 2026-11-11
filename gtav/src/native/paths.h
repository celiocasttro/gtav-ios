// Default locations for native-renderer data and logs: next to the executable.
#pragma once

#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#include <cstdlib>
#include <mach-o/dyld.h>
#elif defined(__ANDROID__)
#include <SDL3/SDL_system.h>
#endif

namespace gtav::native {

inline std::filesystem::path ExeDir() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return std::filesystem::path(std::wstring(buf, n)).parent_path();
#elif defined(__APPLE__) && TARGET_OS_IPHONE
  // The app bundle is read-only: settings, SPIR-V, caches and logs live in the app's Documents
  // (visible in the Files app and Finder file sharing).
  const char* home = std::getenv("HOME");
  return home ? std::filesystem::path(home) / "Documents" : std::filesystem::current_path();
#elif defined(__APPLE__)
  char buf[4096];
  uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) != 0) return std::filesystem::current_path();
  std::error_code ec;
  auto exe = std::filesystem::canonical(buf, ec);
  return ec ? std::filesystem::path(buf).parent_path() : exe.parent_path();
#elif defined(__ANDROID__)
  // The process is app_process: use the app's external files dir (game data, spirv, logs),
  // which adb can write. Only valid once SDL's Java side is up (not from static initializers).
  const char* dir = SDL_GetAndroidExternalStoragePath();
  return dir ? std::filesystem::path(dir) : std::filesystem::current_path();
#else
  std::error_code ec;
  auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
  return ec ? std::filesystem::current_path() : exe.parent_path();
#endif
}

// SPIR-V of the game's shaders (census output): <exe dir>/spirv. iPhone release: inside the app
// bundle (the player's Mac translated them into the ipa); a Documents/spirv (dev builds) wins.
inline std::string DefaultSpirvDir() {
#if defined(__APPLE__) && TARGET_OS_IPHONE
  std::error_code ec;
  if (!std::filesystem::is_directory(ExeDir() / "spirv", ec)) {
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) return (std::filesystem::path(buf).parent_path() / "spirv").string();
  }
#endif
  return (ExeDir() / "spirv").string();
}

// Dumps and probe output: <exe dir>/logs/<sub>.
inline std::string LogPath(const std::string& sub) { return (ExeDir() / "logs" / sub).string(); }

}  // namespace gtav::native
