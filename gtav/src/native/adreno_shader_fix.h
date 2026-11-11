// gtav - Adreno fixes for the game's translated shaders, run on the phone by the background shader
// compiler (C++ ports of tools/android/ubo_constants.py and lower_int64.py; the Python versions
// stay for the PC-side tools, and tools/android/shader_fix_cli.cpp checks the two agree).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gtav::adreno {

// XenosRecomp HLSL: shader constants from uniform buffers (set 4) instead of buffer-device-address
// loads. Returns false (with *error) on a constant load it cannot map.
bool UboConstants(std::string& hlsl, std::string* error);

// SPIR-V assembly text (spirv-dis --raw-id): 64-bit integers lowered to uvec2. Returns false when
// the module has no 64-bit integers (*out untouched) or on an unhandled op (*error set).
bool LowerInt64Text(const std::string& text, std::string* out, std::string* error);

// The same on a binary module, through SPIRV-Tools (disassemble, lower, assemble for Vulkan 1.1).
// A module without 64-bit integers comes back unchanged (returns true).
bool LowerInt64(std::vector<uint32_t>& spirv, std::string* error);

}  // namespace gtav::adreno
