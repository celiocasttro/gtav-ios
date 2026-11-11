// gtav - unhandled host exception reporter (Windows); debugger attach opt-in (Linux).
//
// Writes the exception, registers and a symbolized stack of the faulting thread to
// crash.txt next to the log. Recompiled functions are named sub_XXXXXXXX, so frames map
// straight back to guest addresses.

#ifdef _WIN32

#include <windows.h>

#include <dbghelp.h>

#include <cstdio>
#include <cstring>

#pragma comment(lib, "dbghelp.lib")

namespace {

LONG WINAPI ReportCrash(EXCEPTION_POINTERS* info) {
  FILE* f = std::fopen("crash.txt", "w");
  if (!f) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  const EXCEPTION_RECORD* rec = info->ExceptionRecord;
  CONTEXT ctx = *info->ContextRecord;
  std::fprintf(f, "exception 0x%08lX at %p\n", rec->ExceptionCode, rec->ExceptionAddress);
  if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2) {
    std::fprintf(f, "  %s address 0x%llX\n", rec->ExceptionInformation[0] ? "write" : "read",
                 static_cast<unsigned long long>(rec->ExceptionInformation[1]));
  }
  std::fprintf(f, "rax=%llX rbx=%llX rcx=%llX rdx=%llX\nrsi=%llX rdi=%llX rsp=%llX rbp=%llX\n",
               ctx.Rax, ctx.Rbx, ctx.Rcx, ctx.Rdx, ctx.Rsi, ctx.Rdi, ctx.Rsp, ctx.Rbp);
  std::fprintf(f, "r8=%llX r9=%llX r10=%llX r11=%llX r12=%llX r13=%llX r14=%llX r15=%llX\n",
               ctx.R8, ctx.R9, ctx.R10, ctx.R11, ctx.R12, ctx.R13, ctx.R14, ctx.R15);

  HANDLE process = GetCurrentProcess();
  HANDLE thread = GetCurrentThread();
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
  SymInitialize(process, nullptr, TRUE);

  STACKFRAME64 frame = {};
  frame.AddrPC.Offset = ctx.Rip;
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Offset = ctx.Rbp;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Offset = ctx.Rsp;
  frame.AddrStack.Mode = AddrModeFlat;

  alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
  for (int i = 0; i < 48; i++) {
    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &ctx, nullptr,
                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
        !frame.AddrPC.Offset) {
      break;
    }
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    DWORD64 disp = 0;
    char module[MAX_PATH] = "?";
    if (DWORD64 base = SymGetModuleBase64(process, frame.AddrPC.Offset)) {
      GetModuleFileNameA(reinterpret_cast<HMODULE>(base), module, MAX_PATH);
    }
    if (SymFromAddr(process, frame.AddrPC.Offset, &disp, sym)) {
      std::fprintf(f, "#%02d %s!%s+0x%llX\n", i, std::strrchr(module, '\\') ? std::strrchr(module, '\\') + 1 : module,
                   sym->Name, static_cast<unsigned long long>(disp));
    } else {
      std::fprintf(f, "#%02d %s!0x%llX\n", i, module,
                   static_cast<unsigned long long>(frame.AddrPC.Offset));
    }
  }
  std::fclose(f);
  return EXCEPTION_CONTINUE_SEARCH;
}

// Installed at static-init time; the runtime's own handlers are vectored (first chance),
// so this only sees exceptions nothing else handled.
const bool kInstalled = (SetUnhandledExceptionFilter(ReportCrash), true);

}  // namespace

#endif  // _WIN32

// Android: debuggerd already writes a tombstone with the native stack (adb logcat / bugreport).
#if defined(__linux__) && !defined(__ANDROID__)

#include <sys/prctl.h>

#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <typeinfo>

namespace {

// Let a debugger attach to the running game (gdb -p) under Yama ptrace_scope=1 without
// root, so hangs can be inspected in place.
const bool kPtracerAllowed = (prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0), true);

// Aborts (an uncaught C++ exception, a failed libc check) left no trace: apport ignores
// unpackaged binaries. Write the reason and a stack with module offsets to crash.txt next to
// the binary (addr2line -e <module> <offset>). SIGSEGV stays with the runtime (guest memory).
void WriteStack(const char* reason, const char* detail) {
  int fd = ::open("crash.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) return;
  char line[512];
  int n = std::snprintf(line, sizeof(line), "%s%s%s\n", reason, detail ? ": " : "", detail ? detail : "");
  (void)!::write(fd, line, size_t(n));
  void* frames[64];
  int count = backtrace(frames, 64);
  for (int i = 0; i < count; i++) {
    Dl_info info{};
    const char* module = "?";
    uintptr_t offset = 0;
    if (dladdr(frames[i], &info) && info.dli_fname) {
      module = info.dli_fname;
      offset = uintptr_t(frames[i]) - uintptr_t(info.dli_fbase);
    }
    n = std::snprintf(line, sizeof(line), "  #%d %p %s+0x%zx %s\n", i, frames[i], module, size_t(offset),
                      info.dli_sname ? info.dli_sname : "");
    (void)!::write(fd, line, size_t(n));
  }
  ::close(fd);
}

void OnAbort(int) {
  WriteStack("SIGABRT", nullptr);
  std::signal(SIGABRT, SIG_DFL);
  std::raise(SIGABRT);
}

void OnTerminate() {
  const char* what = nullptr;
  char buf[384] = {};
  if (std::exception_ptr e = std::current_exception()) {
    try {
      std::rethrow_exception(e);
    } catch (const std::exception& ex) {
      std::snprintf(buf, sizeof(buf), "%s: %s", typeid(ex).name(), ex.what());
      what = buf;
    } catch (...) {
      what = "non-std exception";
    }
  }
  WriteStack("std::terminate", what);
  std::signal(SIGABRT, SIG_DFL);
  std::abort();
}

const bool kCrashReporter = (std::signal(SIGABRT, OnAbort), std::set_terminate(OnTerminate), true);

}  // namespace

#endif  // __linux__ && !__ANDROID__
