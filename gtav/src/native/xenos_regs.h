// gtav - reading Xenos registers out of a recorded device register shadow.
//
// The D3D device keeps dense per-block copies of the GPU registers (d3d_map.h); a DrawCommand
// carries the bytes [kRegShadowBegin, kRegShadowEnd) of the device, big-endian.

#pragma once

#include "frame.h"

#include <cstdint>

namespace gtav::native::xenos_regs {

struct Block {
  uint32_t first_reg, device_offset, count;
};

constexpr Block kBlocks[] = {
    {0x4900, 10112, 64}, {0x2000, 10368, 19}, {0x2100, 10444, 21}, {0x2180, 10528, 5},
    {0x2200, 10548, 12}, {0x2280, 10596, 21}, {0x2300, 10680, 38}, {0x2380, 10832, 44},
};

// Returns the register value, or `fallback` when the register is not shadowed.
// Writes a register in a captured shadow copy (no-op for registers outside the captured blocks).
inline void Write(uint8_t* shadow, uint32_t reg, uint32_t value) {
  for (const auto& b : kBlocks) {
    if (reg >= b.first_reg && reg < b.first_reg + b.count) {
      uint8_t* p = shadow + (b.device_offset - kRegShadowBegin) + 4 * (reg - b.first_reg);
      p[0] = uint8_t(value >> 24), p[1] = uint8_t(value >> 16), p[2] = uint8_t(value >> 8), p[3] = uint8_t(value);
      return;
    }
  }
}

inline uint32_t Read(const uint8_t* shadow, uint32_t reg, uint32_t fallback = 0) {
  for (const auto& b : kBlocks) {
    if (reg >= b.first_reg && reg < b.first_reg + b.count) {
      const uint8_t* p = shadow + (b.device_offset - kRegShadowBegin) + 4 * (reg - b.first_reg);
      return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
    }
  }
  return fallback;
}

inline float ReadFloat(const uint8_t* shadow, uint32_t reg) {
  uint32_t v = Read(shadow, reg);
  float f;
  __builtin_memcpy(&f, &v, 4);
  return f;
}

// Register numbers (Xenos register file).
enum : uint32_t {
  RB_SURFACE_INFO = 0x2000,
  RB_COLOR_INFO = 0x2001,
  RB_DEPTH_INFO = 0x2002,
  RB_COLOR1_INFO = 0x2003,
  RB_COLOR2_INFO = 0x2004,
  RB_COLOR3_INFO = 0x2005,
  // D3D keeps PA_SC_WINDOW_SCISSOR_TL/BR (0x2081/0x2082) as dwords 17/18 of its 0x2000 block
  // (written by the viewport/scissor helper sub_83806BE0), so they read as 0x2011/0x2012 here.
  D3D_WINDOW_SCISSOR_TL = 0x2011,
  D3D_WINDOW_SCISSOR_BR = 0x2012,
  RB_COLOR_MASK = 0x2104,
  RB_BLEND_RED = 0x2105,
  RB_STENCILREFMASK_BF = 0x210C,
  RB_STENCILREFMASK = 0x210D,
  RB_ALPHA_REF = 0x210E,
  PA_CL_VPORT_XSCALE = 0x210F,
  PA_CL_VPORT_XOFFSET = 0x2110,
  PA_CL_VPORT_YSCALE = 0x2111,
  PA_CL_VPORT_YOFFSET = 0x2112,
  PA_CL_VPORT_ZSCALE = 0x2113,
  PA_CL_VPORT_ZOFFSET = 0x2114,
  RB_DEPTHCONTROL = 0x2200,
  RB_BLENDCONTROL0 = 0x2201,
  RB_COLORCONTROL = 0x2202,
  PA_CL_CLIP_CNTL = 0x2204,
  PA_SU_SC_MODE_CNTL = 0x2205,
  PA_CL_VTE_CNTL = 0x2206,
  PA_SU_VTX_CNTL = 0x2302,
  RB_MODECONTROL = 0x2208,
  RB_BLENDCONTROL1 = 0x2209,
  PA_SU_POLY_OFFSET_FRONT_SCALE = 0x2380,
  PA_SU_POLY_OFFSET_FRONT_OFFSET = 0x2381,
  PA_SU_POLY_OFFSET_BACK_SCALE = 0x2382,
  PA_SU_POLY_OFFSET_BACK_OFFSET = 0x2383,
};

// Xenos ColorRenderTargetFormat (RB_COLOR_INFO bits 16-19).
enum class ColorFormat : uint32_t {
  k8888 = 0, k8888Gamma = 1, k2_10_10_10 = 2, k2_10_10_10Float = 3, k16_16 = 4, k16_16_16_16 = 5,
  k16_16Float = 6, k16_16_16_16Float = 7, k2_10_10_10As10_10_10_10 = 10,
  k2_10_10_10FloatAs16_16_16_16 = 12, k32Float = 14, k32_32Float = 15,
};

struct SurfaceInfo {
  uint32_t pitch = 0;  // pixels
  uint32_t msaa = 0;   // 0 = 1x, 1 = 2x, 2 = 4x
};

inline SurfaceInfo Surface(const uint8_t* shadow) {
  uint32_t v = Read(shadow, RB_SURFACE_INFO);
  return {v & 0x3FFF, (v >> 16) & 3};
}

struct ColorTarget {
  uint32_t base = 0;  // EDRAM tiles
  ColorFormat format = ColorFormat::k8888;
};

inline ColorTarget Color(const uint8_t* shadow, int index) {
  static constexpr uint32_t kRegs[4] = {RB_COLOR_INFO, RB_COLOR1_INFO, RB_COLOR2_INFO, RB_COLOR3_INFO};
  uint32_t v = Read(shadow, kRegs[index]);
  return {v & 0xFFF, ColorFormat((v >> 16) & 0xF)};
}

struct DepthTarget {
  uint32_t base = 0;
  bool float24 = false;
};

inline DepthTarget Depth(const uint8_t* shadow) {
  uint32_t v = Read(shadow, RB_DEPTH_INFO);
  return {v & 0xFFF, ((v >> 16) & 1) != 0};
}

}  // namespace gtav::native::xenos_regs
