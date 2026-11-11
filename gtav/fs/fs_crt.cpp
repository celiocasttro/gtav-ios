// gtav - the few C runtime symbols the freestanding game DLL needs (see gtav_pch.h).
// clang lowers large struct copies and clears to memcpy/memset calls and references _fltused
// whenever floating point is used; the DLL links no C runtime, so they are defined here.
#include <stddef.h>

extern "C" {

int _fltused = 0;

void* memcpy(void* dst, const void* src, size_t n) {
  void* d = dst;
  __asm__ volatile("rep movsb" : "+D"(d), "+S"(src), "+c"(n) : : "memory");
  return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
  unsigned char* d = static_cast<unsigned char*>(dst);
  const unsigned char* s = static_cast<const unsigned char*>(src);
  if (d <= s || d >= s + n) return memcpy(dst, src, n);
  // Overlapping, destination above the source: copy backwards.
  void* dl = d + n - 1;
  const void* sl = s + n - 1;
  __asm__ volatile("std\n\trep movsb\n\tcld" : "+D"(dl), "+S"(sl), "+c"(n) : : "memory");
  return dst;
}

void* memset(void* dst, int value, size_t n) {
  void* d = dst;
  __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(value) : "memory");
  return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
  const unsigned char* x = static_cast<const unsigned char*>(a);
  const unsigned char* y = static_cast<const unsigned char*>(b);
  for (size_t i = 0; i < n; ++i) {
    if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  }
  return 0;
}

}  // extern "C"
