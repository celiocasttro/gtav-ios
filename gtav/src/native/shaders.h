// gtav - Xenos shader capture (see shaders.cpp).

#pragma once

#include <cstdint>
#include <vector>

namespace gtav::native {

// XenosRecomp cache key of the container a guest D3D shader object was created from, or 0.
uint64_t ShaderHash(uint32_t guest_object);

// Xenia's key for the same shader (XXH3 of its raw microcode), for comparing with the emulator.
uint64_t ShaderUcodeHash(uint32_t guest_object);

// The float constant registers of its bank a shader reads, as runs of consecutive registers
// (small gaps merged), from xenia's ucode analysis. All 256 when it indexes them dynamically or
// the shader is unknown. Draws copy only these; `count` is one past the highest.
struct ConstantUse {
  struct Run {
    uint16_t first, count;
  };
  uint32_t count = 256;
  std::vector<Run> runs{{0, 256}};
};
const ConstantUse& ShaderConstants(uint32_t guest_object);

// Container hash of a created shader with this microcode hash, or 0 (diagnostics).
uint64_t ContainerForUcode(uint64_t ucode_hash);

}  // namespace gtav::native
