#pragma once
// Byte swaps of 16 bytes at a time (big-endian guest data to host order): SSSE3 on x86-64, NEON on
// arm64 (macOS, Android). unit = 4 (32-bit words) or 2 (16-bit halves).

#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define GTAV_SWAP16_TARGET __attribute__((target("ssse3")))
namespace gtav::native {
GTAV_SWAP16_TARGET inline void Swap16Bytes(uint8_t* out, const uint8_t* in, uint32_t unit) {
  const __m128i swap = unit == 4 ? _mm_setr_epi8(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12)
                                 : _mm_setr_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14);
  _mm_storeu_si128(reinterpret_cast<__m128i*>(out),
                   _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(in)), swap));
}
}  // namespace gtav::native
#elif defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define GTAV_SWAP16_TARGET
namespace gtav::native {
inline void Swap16Bytes(uint8_t* out, const uint8_t* in, uint32_t unit) {
  uint8x16_t v = vld1q_u8(in);
  vst1q_u8(out, unit == 4 ? vrev32q_u8(v) : vrev16q_u8(v));
}
}  // namespace gtav::native
#else
#define GTAV_SWAP16_TARGET
namespace gtav::native {
inline void Swap16Bytes(uint8_t* out, const uint8_t* in, uint32_t unit) {
  for (uint32_t i = 0; i < 16; i += unit)
    for (uint32_t b = 0; b < unit; b++) out[i + b] = in[i + unit - 1 - b];
}
}  // namespace gtav::native
#endif
