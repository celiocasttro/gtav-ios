// gtav - guest texture decode (fetch constant -> linear host texel data).

#pragma once

#include <plume_render_interface.h>

#include <cstdint>
#include <vector>

namespace gtav::native {

struct DecodedTexture {
  struct Mip {
    uint32_t offset = 0;     // into data
    uint32_t width = 0, height = 0;
    uint32_t row_texels = 0;  // row pitch in texels (copy footprint)
  };
  plume::RenderFormat format = plume::RenderFormat::UNKNOWN;
  uint32_t width = 0, height = 0, mip_count = 0;
  uint32_t depth = 1;   // > 1 only for volume textures
  bool volume = false;  // 3D texture (base level only; slices stacked in data)
  Mip mips[16];
  std::vector<uint8_t> data;
  plume::RenderComponentMapping swizzle;
};

// Decodes the 2D texture a fetch constant describes (mip chain untiled, endian-swapped,
// unsupported formats converted), or the base level of a 3D texture. Returns false for anything
// not handled yet (cube, stacked, unknown formats); the caller binds a null texture instead.
bool DecodeGuestTexture(const uint32_t words[6], DecodedTexture* out);

// Hash of the texture's raw guest bytes (base + mips): cheap change detection without decoding.
// `sampled` hashes ~5 KB spread over the texture instead (a per-frame check of large textures).
uint64_t HashGuestTexture(const uint32_t words[6], bool sampled = false);

// The guest physical byte ranges a texture reads: base level {address, size}, mips {address, size}
// (size 0 without mips). False if the fetch constant is not a texture.
bool GuestTextureRanges(const uint32_t words[6], uint32_t ranges[4]);

// The guest physical base address of a texture fetch constant (0 if not a texture).
uint32_t FetchBaseAddress(const uint32_t words[6]);

}  // namespace gtav::native
