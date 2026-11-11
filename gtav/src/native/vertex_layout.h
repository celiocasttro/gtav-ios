// gtav - XDK vertex declarations -> host input layouts.
//
// Guest vertex data is big-endian and may use formats desktop GPUs lack (10:10:10:2), so each
// stream is repacked on upload into a host layout: every element is byte-swapped by its own
// component size and the unsupported packed formats are widened. Locations must match the
// USAGE_LOCATIONS table in XenosRecomp (xenosrecomp-gtav.patch).

#pragma once

#include "frame.h"

#include <plume_render_interface.h>

#include <cstdint>

namespace gtav::native::vertex_layout {

// XDK D3DDECLTYPE values (360 encoding).
enum : uint32_t {
  kFloat1 = 0x2C83A4, kFloat2 = 0x2C23A5, kFloat3 = 0x2A23B9, kFloat4 = 0x1A23A6,
  kD3DColor = 0x182886, kUByte4 = 0x1A2286, kUByte4_2 = 0x1A2386, kShort2 = 0x2C2359,
  kShort4 = 0x1A235A, kUByte4N = 0x1A2086, kUByte4N_2 = 0x1A2186, kShort2N = 0x2C2159,
  kShort4N = 0x1A215A, kUShort2N = 0x2C2059, kUShort4N = 0x1A205A, kUInt1 = 0x2C82A1,
  kUDec3 = 0x2A2287, kDec3N = 0x2A2187, kDec4N = 0x1A2187, kDec3N_2 = 0x2A2190,
  kDec3N_3 = 0x2A2390, kFloat16_2 = 0x2C235F, kFloat16_4 = 0x1A2360, kUnused = 0xFFFFFFFF,
};

// D3DDECLUSAGE
enum : uint8_t {
  kPosition = 0, kBlendWeight = 1, kBlendIndices = 2, kNormal = 3, kPointSize = 4, kTexCoord = 5,
  kTangent = 6, kBinormal = 7, kColor = 10,
};

struct TypeInfo {
  uint32_t guest_size;     // bytes in the guest stream
  uint32_t swap;           // byte-swap unit: 2 or 4
  plume::RenderFormat host;
  uint32_t host_size;      // bytes in the repacked host stream
  bool dec = false;        // 10:10:10:2, widened to R16G16B16A16 on upload
  bool dec_signed = false;
  // Non-normalized integers: Xenos vertex fetch hands them to the shader as floats, so they are
  // converted to float32 on upload (int_bits = 8/16/32 per component, int_count components).
  uint32_t int_bits = 0, int_count = 0;
  bool int_signed = false;
};

inline bool Lookup(uint32_t type, TypeInfo* out) {
  using F = plume::RenderFormat;
  switch (type) {
    case kFloat1: *out = {4, 4, F::R32_FLOAT, 4}; return true;
    case kFloat2: *out = {8, 4, F::R32G32_FLOAT, 8}; return true;
    case kFloat3: *out = {12, 4, F::R32G32B32_FLOAT, 12}; return true;
    case kFloat4: *out = {16, 4, F::R32G32B32A32_FLOAT, 16}; return true;
    // Verified against the emulator (G-buffer RT3 red = vertex color x): BGRA is right here.
    case kD3DColor: *out = {4, 4, F::B8G8R8A8_UNORM, 4}; return true;
    case kUByte4:
    case kUByte4_2: *out = {4, 4, F::R32G32B32A32_FLOAT, 16, false, false, 8, 4, false}; return true;
    case kUByte4N:
    case kUByte4N_2: *out = {4, 4, F::R8G8B8A8_UNORM, 4}; return true;
    case kShort2: *out = {4, 2, F::R32G32_FLOAT, 8, false, false, 16, 2, true}; return true;
    case kShort4: *out = {8, 2, F::R32G32B32A32_FLOAT, 16, false, false, 16, 4, true}; return true;
    case kShort2N: *out = {4, 2, F::R16G16_SNORM, 4}; return true;
    case kShort4N: *out = {8, 2, F::R16G16B16A16_SNORM, 8}; return true;
    case kUShort2N: *out = {4, 2, F::R16G16_UNORM, 4}; return true;
    case kUShort4N: *out = {8, 2, F::R16G16B16A16_UNORM, 8}; return true;
    case kUInt1: *out = {4, 4, F::R32_FLOAT, 4, false, false, 32, 1, false}; return true;
    case kFloat16_2: *out = {4, 2, F::R16G16_FLOAT, 4}; return true;
    case kFloat16_4: *out = {8, 2, F::R16G16B16A16_FLOAT, 8}; return true;
    case kUDec3: *out = {4, 4, F::R16G16B16A16_UNORM, 8, true, false}; return true;
    case kDec3N:
    case kDec4N:
    case kDec3N_2:
    case kDec3N_3: *out = {4, 4, F::R16G16B16A16_SNORM, 8, true, true}; return true;
    default: return false;
  }
}

// Vulkan input location per (usage, usage index); ~0u when the shader cannot consume it.
inline uint32_t Location(uint8_t usage, uint8_t index) {
  switch (usage) {
    case kPosition: return index < 4 ? (index == 0 ? 0u : 15u + index) : ~0u;
    case kNormal: return index == 0 ? 1u : index == 1 ? 27u : ~0u;
    case kTangent: return index == 0 ? 2u : index == 1 ? 28u : ~0u;
    case kBinormal: return index == 0 ? 3u : ~0u;
    case kTexCoord: return index < 8 ? 4u + index : index < 16 ? 11u + index : ~0u;
    case kColor: return index < 2 ? 12u + index : ~0u;
    case kBlendIndices: return index == 0 ? 14u : index == 1 ? 29u : ~0u;
    case kBlendWeight: return index == 0 ? 15u : index == 1 ? 30u : ~0u;
    case kPointSize: return index == 0 ? 31u : ~0u;
    default: return ~0u;
  }
}

inline const char* Semantic(uint8_t usage) {
  switch (usage) {
    case kPosition: return "POSITION";
    case kBlendWeight: return "BLENDWEIGHT";
    case kBlendIndices: return "BLENDINDICES";
    case kNormal: return "NORMAL";
    case kPointSize: return "PSIZE";
    case kTexCoord: return "TEXCOORD";
    case kTangent: return "TANGENT";
    case kBinormal: return "BINORMAL";
    case kColor: return "COLOR";
    default: return "TEXCOORD";
  }
}

// Repacked host layout of one draw's declaration.
struct HostLayout {
  uint32_t element_count = 0;
  plume::RenderInputElement elements[kMaxVertexElements];
  uint32_t host_offset[kMaxVertexElements] = {};
  uint32_t guest_index[kMaxVertexElements] = {};  // element in the DrawCommand
  TypeInfo info[kMaxVertexElements] = {};
  uint32_t host_stride[kMaxStreams] = {};
  bool stream_used[kMaxStreams] = {};
};

inline void Build(const DrawCommand& d, HostLayout* out) {
  *out = HostLayout{};
  for (uint32_t i = 0; i < d.element_count; i++) {
    const VertexElement& e = d.elements[i];
    TypeInfo info;
    uint32_t location = Location(e.usage, e.usage_index);
    if (e.stream >= kMaxStreams || !Lookup(e.type, &info) || location == ~0u) continue;
    uint32_t n = out->element_count++;
    out->info[n] = info;
    out->guest_index[n] = i;
    out->host_offset[n] = out->host_stride[e.stream];
    out->host_stride[e.stream] += info.host_size;
    out->stream_used[e.stream] = true;
    out->elements[n] = plume::RenderInputElement(Semantic(e.usage), e.usage_index, location, info.host,
                                                 e.stream, out->host_offset[n]);
  }
}

}  // namespace gtav::native::vertex_layout
