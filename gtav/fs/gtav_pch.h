// gtav - freestanding prefix header for the recompiled game code (gtav_game.dll / gtav_game.so).
//
// The installer runs ReXGlue codegen on the player's own default.xex + TU27 patch and compiles the
// output on their machine with the bundled clang + lld. That compile has no Microsoft headers or
// libraries: no MSVC STL, no UCRT, no Windows SDK. This file replaces the generated gtav_pch.h,
// which pulls in the SDK headers (and through them the STL, spdlog and fmt), with a self-contained
// version that needs only clang's own resource headers (stdint, immintrin).
//
// Everything the generated code needs from the runtime goes through a small C interface exported
// by gtav_fast.exe (gtavfs_*, src/game_bridge.cpp). Kernel imports (__imp__Ke*, __imp__Nt*, ...)
// are imported from the ReXGlue runtime DLL and hooked guest functions from gtav_fast.exe.
// On Linux the same names are left undefined in gtav_game.so and the dynamic loader binds them to
// gtav_fast and librexruntime.so at load time (no import libraries).
//
// Layouts and inline helpers below are taken from the ReXGlue SDK v0.10.0 headers
// (include/rex/ppc/context.h, ppc/intrinsics.h, ppc/func.h, platform/fpscr.h, image_info.h):
//   Copyright (c) 2026, Tom Clay <tomc@tctechstuff.com>. BSD 3-Clause License.
//   Portions derived from the Xenia project, Copyright (c) 2022 Ben Vanik and contributors.
// layout_check.h (also compiled against the SDK headers, in src/game_bridge.cpp) checks that the
// structures here match the SDK's byte for byte.
#pragma once

#define REX_CONFIG_H_INCLUDED

#define REX_IMAGE_BASE 0x82000000ull
#define REX_IMAGE_SIZE 0x1F50000ull
#define REX_CODE_BASE 0x82210000ull
#define REX_CODE_SIZE 0x16A60F4ull
#define REX_THUNK_RESERVE_SIZE 0x10000ull

#define REXCRT_HEAP 0

#include <stddef.h>
#include <stdint.h>
#if defined(__aarch64__)
// arm64 (macOS): the simde headers themselves (MIT, shipped in fs/include), the same SSE-on-NEON
// code the SDK build runs. They need only clang's resource headers (arm_neon.h).
#include <simde/x86/avx.h>
#include <simde/x86/avx2.h>
#include <simde/x86/sse.h>
#include <simde/x86/sse4.1.h>
#else
#include <immintrin.h>
#endif

#ifdef _WIN32
#define GTAVFS_IMPORT __declspec(dllimport)
#else
#define GTAVFS_IMPORT
#endif

#define INT_MAX 2147483647
#define INT_MIN (-INT_MAX - 1)
#define UINT_MAX 4294967295u
#define LLONG_MAX 9223372036854775807LL
#define LLONG_MIN (-LLONG_MAX - 1)

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using f32 = float;
using f64 = double;

//=============================================================================
// The few standard library names the generated code uses
//=============================================================================

namespace std {
enum memory_order { memory_order_relaxed, memory_order_consume, memory_order_acquire,
                    memory_order_release, memory_order_acq_rel, memory_order_seq_cst };
inline void atomic_thread_fence(memory_order) noexcept { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
inline float fma(float a, float b, float c) noexcept { return __builtin_fmaf(a, b, c); }
inline double fma(double a, double b, double c) noexcept { return __builtin_fma(a, b, c); }
inline bool isnan(float v) noexcept { return __builtin_isnan(v); }
inline bool isnan(double v) noexcept { return __builtin_isnan(v); }
template <class To, class From>
constexpr To bit_cast(const From& v) noexcept { return __builtin_bit_cast(To, v); }
}  // namespace std

extern "C" void* memcpy(void*, const void*, size_t);
extern "C" void* memset(void*, int, size_t);

// sqrt is the sqrtsd instruction (the DLL is built with -fno-math-errno). log2f comes from the
// system's ucrtbase.dll (Linux: libm.so.6), the same implementation the SDK build calls, so results
// stay identical.
inline double sqrt(double v) noexcept { return __builtin_sqrt(v); }
inline float sqrt(float v) noexcept { return __builtin_sqrtf(v); }
extern "C" GTAVFS_IMPORT float log2f(float);

#if !defined(__aarch64__)
//=============================================================================
// SIMD: simde names used by the generated code, on native x86-64-v3 intrinsics.
// (simde on an x86-64-v3 target compiles each of these to exactly the native intrinsic.)
//=============================================================================

typedef __m128 simde__m128;
typedef __m128i simde__m128i;
typedef __m128d simde__m128d;

#define SIMDE_MM_ROUND_NEAREST _MM_ROUND_NEAREST
#define SIMDE_MM_ROUND_DOWN _MM_ROUND_DOWN
#define SIMDE_MM_ROUND_UP _MM_ROUND_UP
#define SIMDE_MM_ROUND_TOWARD_ZERO _MM_ROUND_TOWARD_ZERO
#define SIMDE_MM_ROUND_MASK _MM_ROUND_MASK
#define SIMDE_MM_FLUSH_ZERO_MASK _MM_FLUSH_ZERO_MASK
#define SIMDE_MM_FROUND_TO_NEAREST_INT _MM_FROUND_TO_NEAREST_INT
#define SIMDE_MM_FROUND_TO_NEG_INF _MM_FROUND_TO_NEG_INF
#define SIMDE_MM_FROUND_TO_POS_INF _MM_FROUND_TO_POS_INF
#define SIMDE_MM_FROUND_TO_ZERO _MM_FROUND_TO_ZERO
#define SIMDE_MM_FROUND_CUR_DIRECTION _MM_FROUND_CUR_DIRECTION
#define SIMDE_MM_FROUND_NO_EXC _MM_FROUND_NO_EXC

#define simde_mm_add_epi8 _mm_add_epi8
#define simde_mm_add_epi32 _mm_add_epi32
#define simde_mm_add_ps _mm_add_ps
#define simde_mm_alignr_epi8 _mm_alignr_epi8
#define simde_mm_and_ps _mm_and_ps
#define simde_mm_and_si128 _mm_and_si128
#define simde_mm_andnot_ps _mm_andnot_ps
#define simde_mm_andnot_si128 _mm_andnot_si128
#define simde_mm_avg_epu8 _mm_avg_epu8
#define simde_mm_avg_epu16 _mm_avg_epu16
#define simde_mm_blend_ps _mm_blend_ps
#define simde_mm_blendv_epi8 _mm_blendv_epi8
#define simde_mm_blendv_ps _mm_blendv_ps
#define simde_mm_castps_si128 _mm_castps_si128
#define simde_mm_castsi128_ps _mm_castsi128_ps
#define simde_mm_cmpeq_epi8 _mm_cmpeq_epi8
#define simde_mm_cmpeq_epi32 _mm_cmpeq_epi32
#define simde_mm_cmpeq_ps _mm_cmpeq_ps
#define simde_mm_cmpge_ps _mm_cmpge_ps
#define simde_mm_cmpgt_epi8 _mm_cmpgt_epi8
#define simde_mm_cmpgt_epi16 _mm_cmpgt_epi16
#define simde_mm_cmpgt_epi32 _mm_cmpgt_epi32
#define simde_mm_cmpgt_ps _mm_cmpgt_ps
#define simde_mm_cmplt_ps _mm_cmplt_ps
#define simde_mm_cmpunord_ps _mm_cmpunord_ps
#define simde_mm_cvtepi8_epi16 _mm_cvtepi8_epi16
#define simde_mm_cvtepi16_epi32 _mm_cvtepi16_epi32
#define simde_mm_cvtepi32_ps _mm_cvtepi32_ps
#define simde_mm_cvtsd_si64 _mm_cvtsd_si64
#define simde_mm_cvttps_epi32 _mm_cvttps_epi32
#define simde_mm_cvttsd_si32 _mm_cvttsd_si32
#define simde_mm_cvttsd_si64 _mm_cvttsd_si64
#define simde_mm_div_ps _mm_div_ps
#define simde_mm_dp_ps _mm_dp_ps
#define simde_mm_extract_epi8 _mm_extract_epi8
#define simde_mm_getcsr _mm_getcsr
#define simde_mm_insert_ps _mm_insert_ps
#define simde_mm_load_ps _mm_load_ps
#define simde_mm_load_sd _mm_load_sd
#define simde_mm_load_si128 _mm_load_si128
#define simde_mm_max_epu8 _mm_max_epu8
#define simde_mm_max_ps _mm_max_ps
#define simde_mm_min_epu8 _mm_min_epu8
#define simde_mm_min_epu32 _mm_min_epu32
#define simde_mm_min_ps _mm_min_ps
#define simde_mm_movemask_epi8 _mm_movemask_epi8
#define simde_mm_movemask_ps _mm_movemask_ps
#define simde_mm_mul_ps _mm_mul_ps
#define simde_mm_or_ps _mm_or_ps
#define simde_mm_or_si128 _mm_or_si128
#define simde_mm_packs_epi32 _mm_packs_epi32
#define simde_mm_packus_epi16 _mm_packus_epi16
#define simde_mm_packus_epi32 _mm_packus_epi32
#define simde_mm_permute_ps _mm_permute_ps
#define simde_mm_round_ps _mm_round_ps
#define simde_mm_set1_epi8 _mm_set1_epi8
#define simde_mm_set1_epi16 _mm_set1_epi16
#define simde_mm_set1_epi32 _mm_set1_epi32
#define simde_mm_set1_ps _mm_set1_ps
#define simde_mm_setcsr _mm_setcsr
#define simde_mm_setzero_ps _mm_setzero_ps
#define simde_mm_setzero_si128 _mm_setzero_si128
#define simde_mm_shuffle_epi8 _mm_shuffle_epi8
#define simde_mm_shuffle_epi32 _mm_shuffle_epi32
#define simde_mm_slli_epi32 _mm_slli_epi32
#define simde_mm_slli_epi64 _mm_slli_epi64
#define simde_mm_slli_si128 _mm_slli_si128
#define simde_mm_sllv_epi32 _mm_sllv_epi32
#define simde_mm_sqrt_ps _mm_sqrt_ps
#define simde_mm_srai_epi32 _mm_srai_epi32
#define simde_mm_srav_epi32 _mm_srav_epi32
#define simde_mm_srl_epi64 _mm_srl_epi64
#define simde_mm_srli_epi32 _mm_srli_epi32
#define simde_mm_srli_epi64 _mm_srli_epi64
#define simde_mm_srli_si128 _mm_srli_si128
#define simde_mm_srlv_epi32 _mm_srlv_epi32
#define simde_mm_store_ps _mm_store_ps
#define simde_mm_store_si128 _mm_store_si128
#define simde_mm_sub_epi8 _mm_sub_epi8
#define simde_mm_sub_epi32 _mm_sub_epi32
#define simde_mm_sub_ps _mm_sub_ps
#define simde_mm_subs_epi16 _mm_subs_epi16
#define simde_mm_unpackhi_epi8 _mm_unpackhi_epi8
#define simde_mm_unpackhi_epi16 _mm_unpackhi_epi16
#define simde_mm_unpackhi_epi32 _mm_unpackhi_epi32
#define simde_mm_unpackhi_epi64 _mm_unpackhi_epi64
#define simde_mm_unpacklo_epi8 _mm_unpacklo_epi8
#define simde_mm_unpacklo_epi16 _mm_unpacklo_epi16
#define simde_mm_unpacklo_epi32 _mm_unpacklo_epi32
#define simde_mm_xor_ps _mm_xor_ps
#define simde_mm_xor_si128 _mm_xor_si128
#endif  // !__aarch64__

//=============================================================================
// Function type and mapping (SDK ppc/func.h)
//=============================================================================

struct PPCContext;
using PPCFunc = void(PPCContext& ctx, uint8_t* base);

#define REX_JOIN(x, y) x##y
#define REX_XSTRINGIFY(x) #x
#define REX_STRINGIFY(x) REX_XSTRINGIFY(x)
#define REX_FUNC(x) void x([[maybe_unused]] PPCContext& __restrict ctx, uint8_t* base)
#define REX_EXTERN(x) extern "C" REX_FUNC(x)

struct PPCFuncMapping {
  size_t guest;
  PPCFunc* host;
};

namespace rex {
namespace system {
class KernelState;
}
using RegisterModulesFunc = void (*)(system::KernelState*);

struct PPCCodegenFlags {
  bool skip_lr = false;
  bool ctr_as_local = false;
  bool xer_as_local = false;
  bool reserved_as_local = false;
  bool skip_msr = false;
  bool cr_as_local = false;
  bool non_argument_as_local = false;
  bool non_volatile_as_local = false;
};

struct PPCImageInfo {
  u32 code_base;
  u32 code_size;
  u32 image_base;
  u32 image_size;
  const PPCFuncMapping* func_mappings;
  bool rexcrt_heap = false;
  RegisterModulesFunc register_modules = nullptr;
  PPCCodegenFlags codegen_flags{};
};
}  // namespace rex

//=============================================================================
// Runtime interface exported by gtav_fast.exe (src/game_bridge.cpp)
//=============================================================================

extern "C" {
GTAVFS_IMPORT u32 gtavfs_mmio_load(u32 addr);
GTAVFS_IMPORT void gtavfs_mmio_store(u32 addr, u32 value);
GTAVFS_IMPORT u64 gtavfs_query_timebase();
GTAVFS_IMPORT PPCFunc* gtavfs_resolve_indirect(u32 guest_address);
GTAVFS_IMPORT void gtavfs_global_lock_enter();
GTAVFS_IMPORT void gtavfs_global_lock_leave();
GTAVFS_IMPORT u64 gtavfs_global_lock_check();
GTAVFS_IMPORT void gtavfs_trap(PPCContext* ctx, u8* base, u16 trap_type);
[[noreturn]] GTAVFS_IMPORT void gtavfs_unimplemented(const char* what);
}

//=============================================================================
// PPC registers and context (SDK ppc/context.h, platform/fpscr.h)
//=============================================================================

constexpr float kPack2101010_Min10 = __builtin_bit_cast(float, 0x403FFE01u);
constexpr float kPack2101010_Max10 = __builtin_bit_cast(float, 0x404001FFu);
constexpr float kPack2101010_Min2 = __builtin_bit_cast(float, 0x40400000u);
constexpr float kPack2101010_Max2 = __builtin_bit_cast(float, 0x40400003u);

namespace rex::platform {
#if defined(__aarch64__)
struct FPSCRPlatform {
  // FPCR: RMode bits 22-23, FZ (19) and FZ16 (24); guest nearest/zero/+inf/-inf.
  static constexpr size_t RoundShift = 22;
  static constexpr size_t RoundMaskVal = 3 << RoundShift;
  static constexpr size_t FlushMask = (1 << 19) | (1 << 24);
  static constexpr size_t GuestToHost[] = {0 << RoundShift, 3 << RoundShift, 1 << RoundShift,
                                           2 << RoundShift};
  // Trap enables (IOE, DZE, OFE, UFE, IXE, IDE): cleared, ARM's default.
  static constexpr u32 ExceptionMask = (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11) | (1 << 12) |
                                       (1 << 15);
  static inline u32 getcsr() noexcept {
    u64 csr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(csr));
    return csr;
  }
  static inline void setcsr(u32 csr) noexcept { __asm__ __volatile__("msr fpcr, %0" : : "r"(u64(csr))); }
  static inline void InitHostExceptions(u32& csr) noexcept { csr &= ~ExceptionMask; }
};
#else
struct FPSCRPlatform {
  static constexpr size_t RoundShift = 13;
  static constexpr size_t RoundMaskVal = _MM_ROUND_MASK;
  static constexpr size_t FlushMask = _MM_FLUSH_ZERO_MASK | 0x0040;  // FTZ | DAZ
  static constexpr u32 ExceptionMask = (1 << 7) | (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11) | (1 << 12);
  static constexpr size_t GuestToHost[] = {_MM_ROUND_NEAREST, _MM_ROUND_TOWARD_ZERO, _MM_ROUND_UP,
                                           _MM_ROUND_DOWN};
  static inline u32 getcsr() noexcept { return _mm_getcsr(); }
  static inline void setcsr(u32 csr) noexcept { _mm_setcsr(csr); }
  static inline void InitHostExceptions(u32& csr) noexcept { csr |= ExceptionMask; }
};
#endif
}  // namespace rex::platform

namespace rex::ppc {

union Register {
  int8_t s8;
  uint8_t u8;
  int16_t s16;
  uint16_t u16;
  int32_t s32;
  uint32_t u32;
  int64_t s64;
  uint64_t u64;
  float f32;
  double f64;
};

struct XERRegister {
  uint8_t so;
  uint8_t ov;
  uint8_t ca;
};

struct CRRegister {
  uint8_t lt;
  uint8_t gt;
  uint8_t eq;
  union {
    uint8_t so;
    uint8_t un;
  };

  inline uint32_t raw() const noexcept { return (lt << 3) | (gt << 2) | (eq << 1) | so; }

  inline void set_raw(uint32_t value) noexcept {
    lt = (value >> 3) & 1;
    gt = (value >> 2) & 1;
    eq = (value >> 1) & 1;
    so = value & 1;
  }

  template <typename T>
  inline void compare(T left, T right, const XERRegister& xer) noexcept {
    lt = left < right;
    gt = left > right;
    eq = left == right;
    so = xer.so;
  }

  inline void compare(double left, double right) noexcept {
    un = __builtin_isnan(left) || __builtin_isnan(right);
    lt = !un && (left < right);
    gt = !un && (left > right);
    eq = !un && (left == right);
  }

  inline void setFromMask(simde__m128 mask, int imm) noexcept {
    int m = simde_mm_movemask_ps(mask);
    lt = m == imm;
    gt = 0;
    eq = m == 0;
    so = 0;
  }

  inline void setFromMask(simde__m128i mask, int imm) noexcept {
    int m = simde_mm_movemask_epi8(mask);
    lt = m == imm;
    gt = 0;
    eq = m == 0;
    so = 0;
  }
};

union alignas(0x10) VRegister {
  int8_t s8[16];
  uint8_t u8[16];
  int16_t s16[8];
  uint16_t u16[8];
  int32_t s32[4];
  uint32_t u32[4];
  int64_t s64[2];
  uint64_t u64[2];
  float f32[4];
  double f64[2];
};

constexpr uint32_t kRoundNearest = 0x00;
constexpr uint32_t kRoundTowardZero = 0x01;
constexpr uint32_t kRoundUp = 0x02;
constexpr uint32_t kRoundDown = 0x03;
constexpr uint32_t kRoundMask = 0x03;

struct FPSCRRegister {
  uint32_t csr;

  static constexpr size_t HostToGuest[] = {kRoundNearest, kRoundDown, kRoundUp, kRoundTowardZero};

  using Platform = platform::FPSCRPlatform;
  static constexpr size_t RoundShift = Platform::RoundShift;
  static constexpr size_t RoundMaskVal = Platform::RoundMaskVal;
  static constexpr size_t FlushMask = Platform::FlushMask;
  static constexpr uint32_t GuestMask = uint32_t(RoundMaskVal) | uint32_t(FlushMask);

  inline uint32_t getcsr() noexcept { return Platform::getcsr(); }
  inline void setcsr(uint32_t csr) noexcept { Platform::setcsr(csr); }

  inline void restoreGuestBits(uint32_t saved) noexcept {
    csr = (getcsr() & ~GuestMask) | (saved & GuestMask);
    setcsr(csr);
  }

  inline uint32_t loadFromHost() noexcept {
    csr = getcsr();
    return HostToGuest[(csr & RoundMaskVal) >> RoundShift];
  }

  inline void storeFromGuest(uint32_t value) noexcept {
    csr &= ~RoundMaskVal;
    csr |= Platform::GuestToHost[value & kRoundMask];
    setcsr(csr);
  }

  inline void enableFlushModeUnconditional() noexcept {
    csr |= FlushMask;
    setcsr(csr);
  }

  inline void disableFlushModeUnconditional() noexcept {
    csr &= ~FlushMask;
    setcsr(csr);
  }

  inline void enableFlushMode() noexcept {
    if ((csr & FlushMask) != FlushMask) [[unlikely]] {
      csr |= FlushMask;
      setcsr(csr);
    }
  }

  inline void disableFlushMode() noexcept {
    if ((csr & FlushMask) != 0) [[unlikely]] {
      csr &= ~FlushMask;
      setcsr(csr);
    }
  }

  inline void InitHost() noexcept {
    csr = getcsr();
    Platform::InitHostExceptions(csr);
    setcsr(csr);
  }
};

}  // namespace rex::ppc

using PPCRegister = rex::ppc::Register;
using PPCXERRegister = rex::ppc::XERRegister;
using PPCCRRegister = rex::ppc::CRRegister;
using PPCVRegister = rex::ppc::VRegister;
using PPCFPSCRRegister = rex::ppc::FPSCRRegister;

#define GTAVFS_R(n) PPCRegister n;
#define GTAVFS_V(n) PPCVRegister n;

struct alignas(0x40) PPCContext {
  PPCRegister r3;
  PPCRegister r0;
  PPCRegister r1;
  PPCRegister r2;
  PPCRegister r4;
  PPCRegister r5;
  PPCRegister r6;
  PPCRegister r7;
  PPCRegister r8;
  PPCRegister r9;
  PPCRegister r10;
  PPCRegister r11;
  PPCRegister r12;
  PPCRegister r13;
  PPCRegister r14;
  PPCRegister r15;
  PPCRegister r16;
  PPCRegister r17;
  PPCRegister r18;
  PPCRegister r19;
  PPCRegister r20;
  PPCRegister r21;
  PPCRegister r22;
  PPCRegister r23;
  PPCRegister r24;
  PPCRegister r25;
  PPCRegister r26;
  PPCRegister r27;
  PPCRegister r28;
  PPCRegister r29;
  PPCRegister r30;
  PPCRegister r31;

  uint64_t lr;
  PPCRegister ctr;
  PPCXERRegister xer;
  PPCRegister reserved;
  uint32_t msr = 0x200A000;
  PPCCRRegister cr0;
  PPCCRRegister cr1;
  PPCCRRegister cr2;
  PPCCRRegister cr3;
  PPCCRRegister cr4;
  PPCCRRegister cr5;
  PPCCRRegister cr6;
  PPCCRRegister cr7;
  PPCFPSCRRegister fpscr;
  uint8_t vscr_sat = 0;
  uint32_t last_indirect_target = 0;

  GTAVFS_R(f0) GTAVFS_R(f1) GTAVFS_R(f2) GTAVFS_R(f3) GTAVFS_R(f4) GTAVFS_R(f5) GTAVFS_R(f6)
  GTAVFS_R(f7) GTAVFS_R(f8) GTAVFS_R(f9) GTAVFS_R(f10) GTAVFS_R(f11) GTAVFS_R(f12) GTAVFS_R(f13)
  GTAVFS_R(f14) GTAVFS_R(f15) GTAVFS_R(f16) GTAVFS_R(f17) GTAVFS_R(f18) GTAVFS_R(f19)
  GTAVFS_R(f20) GTAVFS_R(f21) GTAVFS_R(f22) GTAVFS_R(f23) GTAVFS_R(f24) GTAVFS_R(f25)
  GTAVFS_R(f26) GTAVFS_R(f27) GTAVFS_R(f28) GTAVFS_R(f29) GTAVFS_R(f30) GTAVFS_R(f31)

  GTAVFS_V(v0) GTAVFS_V(v1) GTAVFS_V(v2) GTAVFS_V(v3) GTAVFS_V(v4) GTAVFS_V(v5) GTAVFS_V(v6)
  GTAVFS_V(v7) GTAVFS_V(v8) GTAVFS_V(v9) GTAVFS_V(v10) GTAVFS_V(v11) GTAVFS_V(v12)
  GTAVFS_V(v13) GTAVFS_V(v14) GTAVFS_V(v15) GTAVFS_V(v16) GTAVFS_V(v17) GTAVFS_V(v18)
  GTAVFS_V(v19) GTAVFS_V(v20) GTAVFS_V(v21) GTAVFS_V(v22) GTAVFS_V(v23) GTAVFS_V(v24)
  GTAVFS_V(v25) GTAVFS_V(v26) GTAVFS_V(v27) GTAVFS_V(v28) GTAVFS_V(v29) GTAVFS_V(v30)
  GTAVFS_V(v31) GTAVFS_V(v32) GTAVFS_V(v33) GTAVFS_V(v34) GTAVFS_V(v35) GTAVFS_V(v36)
  GTAVFS_V(v37) GTAVFS_V(v38) GTAVFS_V(v39) GTAVFS_V(v40) GTAVFS_V(v41) GTAVFS_V(v42)
  GTAVFS_V(v43) GTAVFS_V(v44) GTAVFS_V(v45) GTAVFS_V(v46) GTAVFS_V(v47) GTAVFS_V(v48)
  GTAVFS_V(v49) GTAVFS_V(v50) GTAVFS_V(v51) GTAVFS_V(v52) GTAVFS_V(v53) GTAVFS_V(v54)
  GTAVFS_V(v55) GTAVFS_V(v56) GTAVFS_V(v57) GTAVFS_V(v58) GTAVFS_V(v59) GTAVFS_V(v60)
  GTAVFS_V(v61) GTAVFS_V(v62) GTAVFS_V(v63) GTAVFS_V(v64) GTAVFS_V(v65) GTAVFS_V(v66)
  GTAVFS_V(v67) GTAVFS_V(v68) GTAVFS_V(v69) GTAVFS_V(v70) GTAVFS_V(v71) GTAVFS_V(v72)
  GTAVFS_V(v73) GTAVFS_V(v74) GTAVFS_V(v75) GTAVFS_V(v76) GTAVFS_V(v77) GTAVFS_V(v78)
  GTAVFS_V(v79) GTAVFS_V(v80) GTAVFS_V(v81) GTAVFS_V(v82) GTAVFS_V(v83) GTAVFS_V(v84)
  GTAVFS_V(v85) GTAVFS_V(v86) GTAVFS_V(v87) GTAVFS_V(v88) GTAVFS_V(v89) GTAVFS_V(v90)
  GTAVFS_V(v91) GTAVFS_V(v92) GTAVFS_V(v93) GTAVFS_V(v94) GTAVFS_V(v95) GTAVFS_V(v96)
  GTAVFS_V(v97) GTAVFS_V(v98) GTAVFS_V(v99) GTAVFS_V(v100) GTAVFS_V(v101) GTAVFS_V(v102)
  GTAVFS_V(v103) GTAVFS_V(v104) GTAVFS_V(v105) GTAVFS_V(v106) GTAVFS_V(v107) GTAVFS_V(v108)
  GTAVFS_V(v109) GTAVFS_V(v110) GTAVFS_V(v111) GTAVFS_V(v112) GTAVFS_V(v113) GTAVFS_V(v114)
  GTAVFS_V(v115) GTAVFS_V(v116) GTAVFS_V(v117) GTAVFS_V(v118) GTAVFS_V(v119) GTAVFS_V(v120)
  GTAVFS_V(v121) GTAVFS_V(v122) GTAVFS_V(v123) GTAVFS_V(v124) GTAVFS_V(v125) GTAVFS_V(v126)
  GTAVFS_V(v127)
};

#undef GTAVFS_R
#undef GTAVFS_V

#include "layout_check.h"

//=============================================================================
// Vector helpers (SDK ppc/intrinsics.h)
//=============================================================================

namespace rex::ppc {

inline uint8_t VectorMaskL[] = {
    0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00,
    0xFF, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
    0xFF, 0xFF, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02,
    0xFF, 0xFF, 0xFF, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03,
    0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x0E, 0x0D, 0x0C,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x0E, 0x0D,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x0E,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F,
};

inline uint8_t VectorMaskR[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x02, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x03, 0x02, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x04, 0x03, 0x02, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x05, 0x04, 0x03, 0x02, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00, 0xFF, 0xFF, 0xFF,
    0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00, 0xFF, 0xFF,
    0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00, 0xFF,
};

inline uint8_t VectorShiftTableL[] = {
    0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00,
    0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
    0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02,
    0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03,
    0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04,
    0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05,
    0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06,
    0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07,
    0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08,
    0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09,
    0x19, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A,
    0x1A, 0x19, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B,
    0x1B, 0x1A, 0x19, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C,
    0x1C, 0x1B, 0x1A, 0x19, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D,
    0x1D, 0x1C, 0x1B, 0x1A, 0x19, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E,
    0x1E, 0x1D, 0x1C, 0x1B, 0x1A, 0x19, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F,
};

inline uint8_t VectorShiftTableR[] = {
    0x1F, 0x1E, 0x1D, 0x1C, 0x1B, 0x1A, 0x19, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10,
    0x1E, 0x1D, 0x1C, 0x1B, 0x1A, 0x19, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F,
    0x1D, 0x1C, 0x1B, 0x1A, 0x19, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E,
    0x1C, 0x1B, 0x1A, 0x19, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D,
    0x1B, 0x1A, 0x19, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C,
    0x1A, 0x19, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B,
    0x19, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A,
    0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09,
    0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08,
    0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07,
    0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06,
    0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05,
    0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04,
    0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03,
    0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02,
    0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
};

inline simde__m128i simde_mm_adds_epu32(simde__m128i a, simde__m128i b) {
  return simde_mm_add_epi32(
      a, simde_mm_min_epu32(simde_mm_xor_si128(a, simde_mm_cmpeq_epi32(a, a)), b));
}

inline simde__m128i simde_mm_avg_epi8(simde__m128i a, simde__m128i b) {
  simde__m128i c = simde_mm_set1_epi8(char(128));
  return simde_mm_xor_si128(c,
                            simde_mm_avg_epu8(simde_mm_xor_si128(c, a), simde_mm_xor_si128(c, b)));
}

inline simde__m128i simde_mm_avg_epi16(simde__m128i a, simde__m128i b) {
  simde__m128i c = simde_mm_set1_epi16(short(32768));
  return simde_mm_xor_si128(c,
                            simde_mm_avg_epu16(simde_mm_xor_si128(c, a), simde_mm_xor_si128(c, b)));
}

inline simde__m128i simde_mm_avg_epi32(simde__m128i a, simde__m128i b) {
  simde__m128i sum = simde_mm_add_epi32(simde_mm_srai_epi32(a, 1), simde_mm_srai_epi32(b, 1));
  return simde_mm_add_epi32(sum,
                            simde_mm_and_si128(simde_mm_or_si128(a, b), simde_mm_set1_epi32(1)));
}

inline simde__m128 simde_mm_cvtepu32_ps_(simde__m128i src1) {
  simde__m128i xmm1 = simde_mm_add_epi32(src1, simde_mm_set1_epi32(127));
  simde__m128i xmm0 = simde_mm_slli_epi32(src1, 31 - 8);
  xmm0 = simde_mm_srli_epi32(xmm0, 31);
  xmm0 = simde_mm_add_epi32(xmm0, xmm1);
  xmm0 = simde_mm_srai_epi32(xmm0, 8);
  xmm0 = simde_mm_add_epi32(xmm0, simde_mm_set1_epi32(0x4F800000));
  simde__m128 xmm2 = simde_mm_cvtepi32_ps(src1);
  return simde_mm_blendv_ps(xmm2, simde_mm_castsi128_ps(xmm0), simde_mm_castsi128_ps(src1));
}

inline simde__m128i simde_mm_perm_epi8_(simde__m128i a, simde__m128i b, simde__m128i c) {
  simde__m128i d = simde_mm_set1_epi8(0xF);
  simde__m128i e = simde_mm_sub_epi8(d, simde_mm_and_si128(c, d));
  return simde_mm_blendv_epi8(simde_mm_shuffle_epi8(a, e), simde_mm_shuffle_epi8(b, e),
                              simde_mm_slli_epi32(c, 3));
}

inline simde__m128i simde_mm_cmpgt_epu8(simde__m128i a, simde__m128i b) {
  simde__m128i c = simde_mm_set1_epi8(char(128));
  return simde_mm_cmpgt_epi8(simde_mm_xor_si128(a, c), simde_mm_xor_si128(b, c));
}

inline simde__m128i simde_mm_cmpgt_epu16(simde__m128i a, simde__m128i b) {
  simde__m128i c = simde_mm_set1_epi16(short(32768));
  return simde_mm_cmpgt_epi16(simde_mm_xor_si128(a, c), simde_mm_xor_si128(b, c));
}

inline simde__m128i simde_mm_vctsxs(simde__m128 src1) {
  simde__m128 xmm2 = simde_mm_cmpunord_ps(src1, src1);
  simde__m128i xmm0 = simde_mm_cvttps_epi32(src1);
  simde__m128i xmm1 = simde_mm_cmpeq_epi32(xmm0, simde_mm_set1_epi32(INT_MIN));
  xmm1 = simde_mm_andnot_si128(simde_mm_castps_si128(src1), xmm1);
  simde__m128 dest = simde_mm_blendv_ps(simde_mm_castsi128_ps(xmm0),
                                        simde_mm_castsi128_ps(simde_mm_set1_epi32(INT_MAX)),
                                        simde_mm_castsi128_ps(xmm1));
  return simde_mm_andnot_si128(simde_mm_castps_si128(xmm2), simde_mm_castps_si128(dest));
}

inline simde__m128i simde_mm_vctuxs(simde__m128 src1) {
  simde__m128 nan_mask = simde_mm_cmpunord_ps(src1, src1);
  simde__m128 neg_mask = simde_mm_cmplt_ps(src1, simde_mm_setzero_ps());
  simde__m128 max_val = simde_mm_set1_ps(4294967295.0f);
  simde__m128 overflow_mask = simde_mm_cmpge_ps(src1, max_val);
  simde__m128 clamped = simde_mm_max_ps(src1, simde_mm_setzero_ps());
  clamped = simde_mm_min_ps(clamped, max_val);
  simde__m128 half_range = simde_mm_set1_ps(2147483648.0f);
  simde__m128 high_bit_mask = simde_mm_cmpge_ps(clamped, half_range);
  simde__m128 adjusted = simde_mm_sub_ps(clamped, simde_mm_and_ps(high_bit_mask, half_range));
  simde__m128i low_bits = simde_mm_cvttps_epi32(adjusted);
  simde__m128i high_bit = simde_mm_and_si128(simde_mm_castps_si128(high_bit_mask),
                                             simde_mm_set1_epi32(int(0x80000000u)));
  simde__m128i result = simde_mm_or_si128(low_bits, high_bit);
  result = simde_mm_andnot_si128(simde_mm_castps_si128(nan_mask), result);
  result = simde_mm_andnot_si128(simde_mm_castps_si128(neg_mask), result);
  result = simde_mm_or_si128(
      simde_mm_andnot_si128(simde_mm_castps_si128(overflow_mask), result),
      simde_mm_and_si128(simde_mm_castps_si128(overflow_mask), simde_mm_set1_epi32(-1)));
  return result;
}

inline simde__m128i simde_mm_vsr(simde__m128i a, simde__m128i b) {
  b = simde_mm_srli_epi64(simde_mm_slli_epi64(b, 61), 61);
  return simde_mm_castps_si128(simde_mm_insert_ps(
      simde_mm_castsi128_ps(simde_mm_srl_epi64(a, b)),
      simde_mm_castsi128_ps(simde_mm_srl_epi64(simde_mm_srli_si128(a, 4), b)), 0x10));
}

inline simde__m128i simde_mm_vsl(simde__m128i a, simde__m128i b) {
  int shift = simde_mm_extract_epi8(b, 15) & 0x7;
  if (shift == 0)
    return a;
#if defined(__aarch64__)
  // NEON immediate shifts need a constant count: variable vshlq_u64 (negative = right shift).
  uint64x2_t va = vreinterpretq_u64_s64(simde__m128i_to_neon_i64(a));
  uint64x2_t low_shifted = vshlq_u64(va, vdupq_n_s64(shift));
  uint64x2_t high_carry = vshlq_u64(va, vdupq_n_s64(shift - 64));
  uint64x2_t r = vsetq_lane_u64(vgetq_lane_u64(low_shifted, 0), vdupq_n_u64(0), 0);
  r = vsetq_lane_u64(vgetq_lane_u64(low_shifted, 1) | vgetq_lane_u64(high_carry, 0), r, 1);
  return simde__m128i_from_neon_i64(vreinterpretq_s64_u64(r));
#else
  simde__m128i low_shifted = simde_mm_slli_epi64(a, shift);
  simde__m128i high_carry = simde_mm_srli_epi64(a, 64 - shift);
  high_carry = simde_mm_slli_si128(high_carry, 8);
  return simde_mm_or_si128(low_shifted, high_carry);
#endif
}

inline simde__m128i simde_mm_vslo(simde__m128i a, simde__m128i b) {
  int shift_bytes = (simde_mm_extract_epi8(b, 0) >> 3) & 0xF;
  if (shift_bytes == 0)
    return a;
  if (shift_bytes >= 16)
    return simde_mm_setzero_si128();
  alignas(16) uint8_t src[16], dst[16];
  simde_mm_store_si128((simde__m128i*)src, a);
  __builtin_memset(dst, 0, sizeof(dst));
  __builtin_memcpy(dst + shift_bytes, src, 16 - shift_bytes);
  return simde_mm_load_si128((simde__m128i*)dst);
}

inline simde__m128i simde_mm_vsro(simde__m128i a, simde__m128i b) {
  int shift_bytes = (simde_mm_extract_epi8(b, 0) >> 3) & 0xF;
  if (shift_bytes == 0)
    return a;
  if (shift_bytes >= 16)
    return simde_mm_setzero_si128();
  alignas(16) uint8_t src[16], dst[16];
  simde_mm_store_si128((simde__m128i*)src, a);
  __builtin_memset(dst, 0, sizeof(dst));
  __builtin_memcpy(dst, src + shift_bytes, 16 - shift_bytes);
  return simde_mm_load_si128((simde__m128i*)dst);
}

inline simde__m128i simde_mm_sllv_epi16(simde__m128i a, simde__m128i count) {
  simde__m128i zero = simde_mm_setzero_si128();
  simde__m128i a_lo = simde_mm_unpacklo_epi16(a, zero);
  simde__m128i a_hi = simde_mm_unpackhi_epi16(a, zero);
  simde__m128i s_lo = simde_mm_unpacklo_epi16(count, zero);
  simde__m128i s_hi = simde_mm_unpackhi_epi16(count, zero);
  simde__m128i r_lo = simde_mm_sllv_epi32(a_lo, s_lo);
  simde__m128i r_hi = simde_mm_sllv_epi32(a_hi, s_hi);
  simde__m128i mask16 = simde_mm_set1_epi32(0xFFFF);
  r_lo = simde_mm_and_si128(r_lo, mask16);
  r_hi = simde_mm_and_si128(r_hi, mask16);
  return simde_mm_packus_epi32(r_lo, r_hi);
}

inline simde__m128i simde_mm_srlv_epi16(simde__m128i a, simde__m128i count) {
  simde__m128i zero = simde_mm_setzero_si128();
  simde__m128i a_lo = simde_mm_unpacklo_epi16(a, zero);
  simde__m128i a_hi = simde_mm_unpackhi_epi16(a, zero);
  simde__m128i s_lo = simde_mm_unpacklo_epi16(count, zero);
  simde__m128i s_hi = simde_mm_unpackhi_epi16(count, zero);
  simde__m128i r_lo = simde_mm_srlv_epi32(a_lo, s_lo);
  simde__m128i r_hi = simde_mm_srlv_epi32(a_hi, s_hi);
  return simde_mm_packus_epi32(r_lo, r_hi);
}

inline simde__m128i simde_mm_srav_epi16(simde__m128i a, simde__m128i count) {
  simde__m128i zero = simde_mm_setzero_si128();
  simde__m128i a_lo = simde_mm_srai_epi32(simde_mm_unpacklo_epi16(a, a), 16);
  simde__m128i a_hi = simde_mm_srai_epi32(simde_mm_unpackhi_epi16(a, a), 16);
  simde__m128i s_lo = simde_mm_unpacklo_epi16(count, zero);
  simde__m128i s_hi = simde_mm_unpackhi_epi16(count, zero);
  simde__m128i r_lo = simde_mm_srav_epi32(a_lo, s_lo);
  simde__m128i r_hi = simde_mm_srav_epi32(a_hi, s_hi);
  return simde_mm_packs_epi32(r_lo, r_hi);
}

inline simde__m128i simde_mm_sllv_epi8(simde__m128i a, simde__m128i count) {
  simde__m128i zero = simde_mm_setzero_si128();
  simde__m128i a_lo = simde_mm_unpacklo_epi8(a, zero);
  simde__m128i a_hi = simde_mm_unpackhi_epi8(a, zero);
  simde__m128i s_lo = simde_mm_unpacklo_epi8(count, zero);
  simde__m128i s_hi = simde_mm_unpackhi_epi8(count, zero);
  simde__m128i r_lo = simde_mm_sllv_epi16(a_lo, s_lo);
  simde__m128i r_hi = simde_mm_sllv_epi16(a_hi, s_hi);
  simde__m128i mask8 = simde_mm_set1_epi16(0xFF);
  r_lo = simde_mm_and_si128(r_lo, mask8);
  r_hi = simde_mm_and_si128(r_hi, mask8);
  return simde_mm_packus_epi16(r_lo, r_hi);
}

}  // namespace rex::ppc

using rex::ppc::VectorMaskL;
using rex::ppc::VectorMaskR;
using rex::ppc::VectorShiftTableL;
using rex::ppc::VectorShiftTableR;

//=============================================================================
// Image info
//=============================================================================

extern const rex::PPCImageInfo PPCImageConfig;
extern PPCFuncMapping PPCFuncMappings[];

//=============================================================================
// Function definitions
//
// gtav_hooks.h lists the guest functions gtav_fast.exe replaces (GTAVFS_HOOKED_<name>). For those
// the generated body is only __imp__<name> (exported, so the hook can call the original) and every
// call to <name> resolves to the hook through gtav_fast.exe's import library (Linux: the dynamic
// loader, from gtav_fast's dynamic symbol table). Every other function
// gets <name> as a plain alias of __imp__<name>.
//=============================================================================

#include "gtav_hooks.h"

#define GTAVFS_SECOND(a, b, ...) b
#define GTAVFS_IS_HOOKED_(...) GTAVFS_SECOND(__VA_ARGS__, 0, ~)
#define GTAVFS_IS_HOOKED(name) GTAVFS_IS_HOOKED_(GTAVFS_HOOKED_##name)
#define GTAVFS_CAT(a, b) GTAVFS_CAT_(a, b)
#define GTAVFS_CAT_(a, b) a##b
#if defined(__APPLE__)
// Mach-O has no __attribute__((alias)); an assembler equate gives <name> the same address.
// (It cannot be overridden, which is fine here: hooked functions never get one.)
#define GTAVFS_DEFINE_0(name)                                      \
  __asm__(".globl _" #name "\n.set _" #name ", ___imp__" #name); \
  extern "C" REX_FUNC(name);                                       \
  REX_EXTERN(__imp__##name)
#else
#define GTAVFS_DEFINE_0(name)                          \
  __attribute__((alias("__imp__" #name), noinline)) \
  extern "C" REX_FUNC(name);                         \
  REX_EXTERN(__imp__##name)
#endif
#define GTAVFS_DEFINE_1(name) REX_EXTERN(__imp__##name)

#define DECLARE_REX_FUNC(name) \
  REX_EXTERN(name);            \
  REX_EXTERN(__imp__##name)

#define DEFINE_REX_FUNC(name) GTAVFS_CAT(GTAVFS_DEFINE_, GTAVFS_IS_HOOKED(name))(name)

#if defined(__clang__)
#define REX_FUNC_PROLOGUE() __builtin_assume(((size_t)base & 0x1F) == 0)
#else
#define REX_FUNC_PROLOGUE() ((void)0)
#endif

//=============================================================================
// Memory access
//=============================================================================

// The SDK maps the E0 physical view 4 KB higher only where the host's allocation granularity
// needs it (pch_h.inja: REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && REX_ARCH_ARM64)); on Linux
// every view starts at its guest address.
#if defined(_WIN32) || (defined(__APPLE__) && defined(__aarch64__))
#define REX_PHYS_HOST_OFFSET(addr) (((u32)(addr) >= 0xE0000000u) ? 0x1000u : 0u)
#else
#define REX_PHYS_HOST_OFFSET(addr) 0u
#endif

#define REX_RAW_ADDR(x) (base + (u32)(x) + REX_PHYS_HOST_OFFSET(x))

#define REX_LOAD_U8(x) (*(volatile u8*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#define REX_LOAD_U16(x) __builtin_bswap16(*(volatile u16*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#define REX_LOAD_U32(x) __builtin_bswap32(*(volatile u32*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#define REX_LOAD_U64(x) __builtin_bswap64(*(volatile u64*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))

#define REX_STORE_U8(x, y)  (*(volatile u8*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = (y))
#define REX_STORE_U16(x, y) (*(volatile u16*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap16(y))
#define REX_STORE_U32(x, y) (*(volatile u32*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap32(y))
#define REX_STORE_U64(x, y) (*(volatile u64*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap64(y))

#define REX_MEMORY_SIZE 0x100000000ull

#define REX_IS_MMIO_ADDR(addr) ((addr) >= 0x7F000000u && (addr) < 0x80000000u)

#define REX_MM_STORE_U8(addr, val)                                                   \
  do {                                                                               \
    u32 _mmio_addr = (addr);                                                         \
    if (REX_IS_MMIO_ADDR(_mmio_addr)) {                                              \
      gtavfs_mmio_store(_mmio_addr, static_cast<u32>(val));                          \
    } else {                                                                         \
      *(volatile u8*)(base + _mmio_addr + REX_PHYS_HOST_OFFSET(_mmio_addr)) = (val); \
    }                                                                                \
  } while (0)

#define REX_MM_STORE_U16(addr, val)                                              \
  do {                                                                           \
    u32 _mmio_addr = (addr);                                                     \
    if (REX_IS_MMIO_ADDR(_mmio_addr)) {                                          \
      gtavfs_mmio_store(_mmio_addr, static_cast<u32>(val));                      \
    } else {                                                                     \
      *(volatile u16*)(base + _mmio_addr + REX_PHYS_HOST_OFFSET(_mmio_addr)) =   \
          __builtin_bswap16(val);                                                \
    }                                                                            \
  } while (0)

#define REX_MM_STORE_U32(addr, val)                                              \
  do {                                                                           \
    u32 _mmio_addr = (addr);                                                     \
    if (REX_IS_MMIO_ADDR(_mmio_addr)) {                                          \
      gtavfs_mmio_store(_mmio_addr, static_cast<u32>(val));                      \
    } else {                                                                     \
      *(volatile u32*)(base + _mmio_addr + REX_PHYS_HOST_OFFSET(_mmio_addr)) =   \
          __builtin_bswap32(val);                                                \
    }                                                                            \
  } while (0)

#define REX_MM_STORE_U64(addr, val)                                              \
  do {                                                                           \
    u32 _mmio_addr = (addr);                                                     \
    if (REX_IS_MMIO_ADDR(_mmio_addr)) {                                          \
      u64 _v64 = static_cast<u64>(val);                                          \
      gtavfs_mmio_store(_mmio_addr, static_cast<u32>(_v64 >> 32));               \
      gtavfs_mmio_store(_mmio_addr + 4, static_cast<u32>(_v64));                 \
    } else {                                                                     \
      *(volatile u64*)(base + _mmio_addr + REX_PHYS_HOST_OFFSET(_mmio_addr)) =   \
          __builtin_bswap64(val);                                                \
    }                                                                            \
  } while (0)

#define REX_MM_LOAD_U8(addr)                                    \
  (REX_IS_MMIO_ADDR(addr) ? static_cast<u8>(gtavfs_mmio_load(addr)) \
                          : *(volatile u8*)(base + (addr) + REX_PHYS_HOST_OFFSET(addr)))

#define REX_MM_LOAD_U16(addr)                                    \
  (REX_IS_MMIO_ADDR(addr) ? static_cast<u16>(gtavfs_mmio_load(addr)) \
                          : __builtin_bswap16(*(volatile u16*)(base + (addr) + REX_PHYS_HOST_OFFSET(addr))))

#define REX_MM_LOAD_U32(addr)                       \
  (REX_IS_MMIO_ADDR(addr) ? gtavfs_mmio_load(addr) \
                          : __builtin_bswap32(*(volatile u32*)(base + (addr) + REX_PHYS_HOST_OFFSET(addr))))

#define REX_MM_LOAD_U64(addr)                                                                \
  (REX_IS_MMIO_ADDR(addr)                                                                    \
       ? ((static_cast<u64>(gtavfs_mmio_load(addr)) << 32) | gtavfs_mmio_load((addr) + 4)) \
       : __builtin_bswap64(*(volatile u64*)(base + (addr) + REX_PHYS_HOST_OFFSET(addr))))

//=============================================================================
// Calls
//=============================================================================

#define REX_CALL_FUNC(x) x(ctx, base)

#define REX_LOOKUP_FUNC(x, y) \
  (*(PPCFunc**)(x + REX_IMAGE_BASE + REX_IMAGE_SIZE + (u64(u32(y) - REX_CODE_BASE) * 2)))

#define REX_CALL_INDIRECT_FUNC(x)                                               \
  do {                                                                          \
    uint32_t rex_indirect_target_ = (uint32_t)(x);                              \
    PPCFunc* rex_indirect_func_;                                                \
    if ((uint32_t)(rex_indirect_target_ - REX_CODE_BASE) <                      \
        REX_CODE_SIZE + REX_THUNK_RESERVE_SIZE) [[likely]] {                    \
      rex_indirect_func_ = REX_LOOKUP_FUNC(base, rex_indirect_target_);         \
    } else {                                                                    \
      rex_indirect_func_ = nullptr;                                             \
    }                                                                           \
    if (!rex_indirect_func_) [[unlikely]] {                                     \
      ctx.last_indirect_target = rex_indirect_target_;                          \
      rex_indirect_func_ = gtavfs_resolve_indirect(rex_indirect_target_);       \
    }                                                                           \
    rex_indirect_func_(ctx, base);                                              \
  } while (0)

//=============================================================================
// Misc
//=============================================================================

#define REX_SET_FLUSH_MODE(enable) \
  do { if (enable) ctx.fpscr.enableFlushMode(); else ctx.fpscr.disableFlushMode(); } while (0)

#define REX_UNIMPLEMENTED(addr, opcode) gtavfs_unimplemented("Unimplemented PPC instruction: " opcode)

#define REX_QUERY_TIMEBASE() gtavfs_query_timebase()

// Memory barriers (sync, lwsync, eieio; codegen builders/system.cpp). x86: lwsync/eieio are only
// compiler barriers; ARM64 (Android, iPhone, Mac) needs the dmb: the game releases its locks
// with "lwsync; stw 0", and without it the unlock can become visible before the writes made
// under the lock (another thread then reads a half-built list).
#define REX_SYNC() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define REX_LWSYNC() __atomic_thread_fence(__ATOMIC_ACQ_REL)
#define REX_EIEIO() __atomic_thread_fence(__ATOMIC_RELEASE)

inline void ppc_trap(PPCContext& ctx, u8* base, u16 trap_type) {
  if (trap_type != 25) gtavfs_trap(&ctx, base, trap_type);
}

#define REX_CHECK_GLOBAL_LOCK() gtavfs_global_lock_check()
#define REX_ENTER_GLOBAL_LOCK() gtavfs_global_lock_enter()
#define REX_LEAVE_GLOBAL_LOCK() gtavfs_global_lock_leave()
