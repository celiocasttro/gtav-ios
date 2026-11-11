// gtav - guest texture decode, ported from the Skate 3 native renderer's
// EnsureGuestTextureFromWords (same SDK texture helpers): packed small mips are located with
// GetMipLocation, tiled mips are sized with the swizzle-aware upper bound and untiled in
// contiguous runs, and every mip is endian-swapped per the fetch constant.

#include "textures.h"

#include <rex/graphics/pipeline/texture/info.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/graphics/xenos.h>
#include <rex/kernel.h>
#include <rex/system.h>

#define XXH_INLINE_ALL
#include <xxhash.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>

namespace gtav::native {
namespace {

namespace xenos = rex::graphics::xenos;
using plume::RenderFormat;
using plume::RenderSwizzle;

enum class Convert { kNone, k565, k1555, k4444, k2_10_10_10, kBC1, kBC2, kBC3, kBC4, kBC5 };

// Apple's phone GPUs have no BC (DXT) formats: on iOS block-compressed textures are decoded on the
// CPU. A block row of the guest becomes the block's 4 texel rows, one after the other, so a decoded
// block row is `cols * host_bytes_per_block` bytes like every other conversion here.
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && TARGET_OS_IPHONE
constexpr bool kDecodeBC = true;
#else
constexpr bool kDecodeBC = false;
#endif

struct HostFormat {
  RenderFormat format;
  Convert convert;
  uint32_t host_bytes_per_block;  // after conversion
};

bool GetHostFormat(xenos::TextureFormat format, HostFormat* out) {
  switch (rex::graphics::GetBaseFormat(format)) {
    case xenos::TextureFormat::k_DXT1:
      *out = kDecodeBC ? HostFormat{RenderFormat::R8G8B8A8_UNORM, Convert::kBC1, 64}
                       : HostFormat{RenderFormat::BC1_UNORM, Convert::kNone, 8};
      return true;
    case xenos::TextureFormat::k_DXT2_3:
      *out = kDecodeBC ? HostFormat{RenderFormat::R8G8B8A8_UNORM, Convert::kBC2, 64}
                       : HostFormat{RenderFormat::BC2_UNORM, Convert::kNone, 16};
      return true;
    case xenos::TextureFormat::k_DXT4_5:
      *out = kDecodeBC ? HostFormat{RenderFormat::R8G8B8A8_UNORM, Convert::kBC3, 64}
                       : HostFormat{RenderFormat::BC3_UNORM, Convert::kNone, 16};
      return true;
    case xenos::TextureFormat::k_DXT5A:
      *out = kDecodeBC ? HostFormat{RenderFormat::R8_UNORM, Convert::kBC4, 16}
                       : HostFormat{RenderFormat::BC4_UNORM, Convert::kNone, 8};
      return true;
    case xenos::TextureFormat::k_DXN:
      *out = kDecodeBC ? HostFormat{RenderFormat::R8G8_UNORM, Convert::kBC5, 32}
                       : HostFormat{RenderFormat::BC5_UNORM, Convert::kNone, 16};
      return true;
    case xenos::TextureFormat::k_8_8_8_8: *out = {RenderFormat::R8G8B8A8_UNORM, Convert::kNone, 4}; return true;
    case xenos::TextureFormat::k_8: *out = {RenderFormat::R8_UNORM, Convert::kNone, 1}; return true;
    case xenos::TextureFormat::k_8_8: *out = {RenderFormat::R8G8_UNORM, Convert::kNone, 2}; return true;
    case xenos::TextureFormat::k_5_6_5: *out = {RenderFormat::R8G8B8A8_UNORM, Convert::k565, 4}; return true;
    case xenos::TextureFormat::k_1_5_5_5: *out = {RenderFormat::R8G8B8A8_UNORM, Convert::k1555, 4}; return true;
    case xenos::TextureFormat::k_4_4_4_4: *out = {RenderFormat::R8G8B8A8_UNORM, Convert::k4444, 4}; return true;
    case xenos::TextureFormat::k_2_10_10_10:
      *out = {RenderFormat::R16G16B16A16_UNORM, Convert::k2_10_10_10, 8};
      return true;
    case xenos::TextureFormat::k_16: *out = {RenderFormat::R16_UNORM, Convert::kNone, 2}; return true;
    case xenos::TextureFormat::k_16_16: *out = {RenderFormat::R16G16_UNORM, Convert::kNone, 4}; return true;
    case xenos::TextureFormat::k_16_16_16_16:
      *out = {RenderFormat::R16G16B16A16_UNORM, Convert::kNone, 8};
      return true;
    case xenos::TextureFormat::k_16_FLOAT: *out = {RenderFormat::R16_FLOAT, Convert::kNone, 2}; return true;
    case xenos::TextureFormat::k_16_16_FLOAT: *out = {RenderFormat::R16G16_FLOAT, Convert::kNone, 4}; return true;
    case xenos::TextureFormat::k_16_16_16_16_FLOAT:
      *out = {RenderFormat::R16G16B16A16_FLOAT, Convert::kNone, 8};
      return true;
    case xenos::TextureFormat::k_32_FLOAT: *out = {RenderFormat::R32_FLOAT, Convert::kNone, 4}; return true;
    case xenos::TextureFormat::k_32_32_FLOAT: *out = {RenderFormat::R32G32_FLOAT, Convert::kNone, 8}; return true;
    case xenos::TextureFormat::k_32_32_32_32_FLOAT:
      *out = {RenderFormat::R32G32B32A32_FLOAT, Convert::kNone, 16};
      return true;
    default:
      return false;
  }
}

void SwapEndian(uint8_t* data, uint32_t size, xenos::Endian endian) {
  switch (endian) {
    case xenos::Endian::k8in16:
      for (uint32_t i = 0; i + 2 <= size; i += 2) std::swap(data[i], data[i + 1]);
      break;
    case xenos::Endian::k8in32:
      for (uint32_t i = 0; i + 4 <= size; i += 4) {
        std::swap(data[i], data[i + 3]);
        std::swap(data[i + 1], data[i + 2]);
      }
      break;
    case xenos::Endian::k16in32:
      for (uint32_t i = 0; i + 4 <= size; i += 4) {
        std::swap(data[i], data[i + 2]);
        std::swap(data[i + 1], data[i + 3]);
      }
      break;
    default:
      break;
  }
}

uint8_t Expand(uint32_t v, uint32_t bits) { return uint8_t(v * 255 / ((1u << bits) - 1)); }

// BC colour endpoints + indices (8 bytes) -> 4x4 RGBA8, row r of the block at out + r * pitch.
void DecodeBC1Colour(const uint8_t* b, uint8_t* out, size_t pitch, bool allow_punch_through) {
  const uint16_t c0 = uint16_t(b[0] | b[1] << 8), c1 = uint16_t(b[2] | b[3] << 8);
  uint8_t pal[4][4];
  auto rgb = [](uint16_t c, uint8_t* o) {
    o[0] = Expand(c >> 11, 5), o[1] = Expand((c >> 5) & 0x3F, 6), o[2] = Expand(c & 0x1F, 5), o[3] = 255;
  };
  rgb(c0, pal[0]);
  rgb(c1, pal[1]);
  if (c0 > c1 || !allow_punch_through) {
    for (int k = 0; k < 3; k++) {
      pal[2][k] = uint8_t((2 * pal[0][k] + pal[1][k] + 1) / 3);
      pal[3][k] = uint8_t((pal[0][k] + 2 * pal[1][k] + 1) / 3);
    }
    pal[2][3] = pal[3][3] = 255;
  } else {
    for (int k = 0; k < 3; k++) pal[2][k] = uint8_t((pal[0][k] + pal[1][k] + 1) / 2);
    pal[2][3] = 255;
    pal[3][0] = pal[3][1] = pal[3][2] = pal[3][3] = 0;
  }
  const uint32_t idx = uint32_t(b[4] | b[5] << 8 | b[6] << 16 | uint32_t(b[7]) << 24);
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 4; x++) std::memcpy(out + y * pitch + x * 4, pal[(idx >> (2 * (y * 4 + x))) & 3], 4);
}

// BC4 block (8 bytes) -> 16 values, written at out + (y * pitch + x * stride).
void DecodeBC4(const uint8_t* b, uint8_t* out, size_t pitch, size_t stride) {
  uint8_t v[8];
  v[0] = b[0];
  v[1] = b[1];
  if (v[0] > v[1]) {
    for (int i = 1; i < 7; i++) v[i + 1] = uint8_t(((7 - i) * v[0] + i * v[1] + 3) / 7);
  } else {
    for (int i = 1; i < 5; i++) v[i + 1] = uint8_t(((5 - i) * v[0] + i * v[1] + 2) / 5);
    v[6] = 0;
    v[7] = 255;
  }
  uint64_t idx = 0;
  for (int i = 0; i < 6; i++) idx |= uint64_t(b[2 + i]) << (8 * i);
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 4; x++) out[y * pitch + x * stride] = v[(idx >> (3 * (y * 4 + x))) & 7];
}

// One guest block row of `blocks` BC blocks -> 4 host texel rows (each blocks * 4 texels wide).
void DecodeBCRow(Convert c, const uint8_t* src, uint8_t* dst, uint32_t blocks) {
  const size_t texel_bytes = c == Convert::kBC4 ? 1 : c == Convert::kBC5 ? 2 : 4;
  const size_t pitch = size_t(blocks) * 4 * texel_bytes;
  for (uint32_t i = 0; i < blocks; i++) {
    uint8_t* o = dst + size_t(i) * 4 * texel_bytes;
    switch (c) {
      case Convert::kBC1: DecodeBC1Colour(src + 8 * i, o, pitch, true); break;
      case Convert::kBC2: {
        const uint8_t* b = src + 16 * i;
        DecodeBC1Colour(b + 8, o, pitch, false);
        for (int y = 0; y < 4; y++)
          for (int x = 0; x < 4; x++) {
            const uint8_t a = (b[y * 2 + x / 2] >> (4 * (x & 1))) & 0xF;
            o[y * pitch + x * 4 + 3] = uint8_t(a * 17);
          }
        break;
      }
      case Convert::kBC3: {
        const uint8_t* b = src + 16 * i;
        DecodeBC1Colour(b + 8, o, pitch, false);
        DecodeBC4(b, o + 3, pitch, 4);
        break;
      }
      case Convert::kBC4: DecodeBC4(src + 8 * i, o, pitch, 1); break;
      case Convert::kBC5:
        DecodeBC4(src + 16 * i, o, pitch, 2);
        DecodeBC4(src + 16 * i + 8, o + 1, pitch, 2);
        break;
      default: break;
    }
  }
}

// Converts one row of guest texels (already host-endian) into host texels (a block row for BC).
void ConvertRow(Convert c, const uint8_t* src, uint8_t* dst, uint32_t texels) {
  if (c >= Convert::kBC1) {
    DecodeBCRow(c, src, dst, texels);
    return;
  }
  for (uint32_t i = 0; i < texels; i++) {
    if (c == Convert::k2_10_10_10) {
      uint32_t v;
      std::memcpy(&v, src + 4 * i, 4);
      uint16_t o[4] = {uint16_t((v & 0x3FF) * 65535 / 1023), uint16_t(((v >> 10) & 0x3FF) * 65535 / 1023),
                       uint16_t(((v >> 20) & 0x3FF) * 65535 / 1023), uint16_t((v >> 30) * 65535 / 3)};
      std::memcpy(dst + 8 * i, o, 8);
      continue;
    }
    uint16_t v;
    std::memcpy(&v, src + 2 * i, 2);
    uint8_t* o = dst + 4 * i;
    switch (c) {
      case Convert::k565:  // R in the low bits on Xenos (the SDK swaps R/B for B5G6R5 hosts)
        o[0] = Expand(v & 0x1F, 5), o[1] = Expand((v >> 5) & 0x3F, 6), o[2] = Expand(v >> 11, 5), o[3] = 255;
        break;
      case Convert::k1555:
        o[0] = Expand(v & 0x1F, 5), o[1] = Expand((v >> 5) & 0x1F, 5), o[2] = Expand((v >> 10) & 0x1F, 5);
        o[3] = (v >> 15) ? 255 : 0;
        break;
      case Convert::k4444:
        o[0] = Expand(v & 0xF, 4), o[1] = Expand((v >> 4) & 0xF, 4), o[2] = Expand((v >> 8) & 0xF, 4);
        o[3] = Expand(v >> 12, 4);
        break;
      default:
        break;
    }
  }
}

RenderSwizzle ToPlume(uint32_t xenos_component) {
  switch (xenos_component) {
    case 0: return RenderSwizzle::R;
    case 1: return RenderSwizzle::G;
    case 2: return RenderSwizzle::B;
    case 3: return RenderSwizzle::A;
    case 4: return RenderSwizzle::ZERO;
    default: return RenderSwizzle::ONE;
  }
}

// Which host component holds each guest component (X,Y,Z,W) of a format, as the SDK's texture
// cache sets up (texture_cache.cpp host swizzles): single-channel formats replicate R, two-channel
// ones replicate G into Z/W, alpha-less ones repeat B in W. The fetch swizzle selects guest
// components, so it has to go through this first: DXT5A specular masks fetched .www read the
// BC4 texture's constant alpha (1) instead of the mask.
std::array<uint32_t, 4> HostComponents(xenos::TextureFormat f) {
  using T = xenos::TextureFormat;
  switch (f) {
    case T::k_1_REVERSE: case T::k_1: case T::k_8: case T::k_8_A: case T::k_8_B: case T::k_24_8:
    case T::k_24_8_FLOAT: case T::k_16: case T::k_16_EXPAND: case T::k_16_FLOAT: case T::k_32:
    case T::k_32_FLOAT: case T::k_32_AS_8: case T::k_DXT3A: case T::k_DXT5A:
      return {0, 0, 0, 0};
    case T::k_8_8: case T::k_16_16_EDRAM: case T::k_16_16: case T::k_16_16_EXPAND: case T::k_16_16_FLOAT:
    case T::k_32_32: case T::k_32_32_FLOAT: case T::k_32_AS_8_8: case T::k_DXN: case T::k_CTX1:
      return {0, 1, 1, 1};
    case T::k_5_6_5: case T::k_10_11_11: case T::k_11_11_10: case T::k_10_11_11_AS_16_16_16_16:
    case T::k_11_11_10_AS_16_16_16_16: case T::k_32_32_32_FLOAT: case T::k_Cr_Y1_Cb_Y0_REP:
    case T::k_Y1_Cr_Y0_Cb_REP:
      return {0, 1, 2, 2};
    default:
      return {0, 1, 2, 3};
  }
}

plume::RenderComponentMapping Swizzle(xenos::TextureFormat format, uint32_t swizzle) {
  auto host = HostComponents(format);
  auto c = [&](uint32_t i) {
    uint32_t s = (swizzle >> (3 * i)) & 7;
    return ToPlume(s < 4 ? host[s] : s);
  };
  return plume::RenderComponentMapping(c(0), c(1), c(2), c(3));
}

}  // namespace

uint32_t FetchBaseAddress(const uint32_t words[6]) {
  if ((words[0] & 3) != uint32_t(xenos::FetchConstantType::kTexture)) return 0;
  return words[1] & 0xFFFFF000;
}

uint64_t HashGuestTexture(const uint32_t words[6], bool sampled) {
  xenos::xe_gpu_texture_fetch_t fetch = {};
  fetch.dword_0 = words[0];
  fetch.dword_1 = words[1];
  fetch.dword_2 = words[2];
  fetch.dword_3 = words[3];
  fetch.dword_4 = words[4];
  fetch.dword_5 = words[5];
  if (fetch.type != xenos::FetchConstantType::kTexture || fetch.base_address == 0) return 0;
  const auto* pre = rex::graphics::FormatInfo::Get(uint32_t(fetch.format));
  if (!pre || !pre->block_width || !pre->block_height || !pre->bytes_per_block()) return 0;
  rex::graphics::TextureInfo info;
  if (!rex::graphics::TextureInfo::Prepare(fetch, &info)) return 0;
  auto* mem = REX_KERNEL_MEMORY();
  auto hash_range = [&](uint32_t addr, uint32_t size, uint64_t seed) {
    if (!addr || !size || size > (64u << 20) || (addr & 0x1FFFFFFF) + uint64_t(size) > 0x20000000) return seed;
    return XXH3_64bits_withSeed(mem->TranslatePhysical(addr), size, seed);
  };
  if (sampled) {
    // 16 runs of 256 bytes spread over the base level and 4 over the mips: catches a streaming
    // slot that now holds another texture, at ~5 KB per check.
    auto sample = [&](uint32_t addr, uint32_t size, uint32_t runs, uint64_t seed) {
      if (size <= runs * 256u) return hash_range(addr, size, seed);
      uint32_t step = (size - 256) / (runs - 1) & ~255u;
      for (uint32_t i = 0; i < runs; i++) seed = hash_range(addr + i * step, 256, seed);
      return seed;
    };
    uint64_t h = sample(info.memory.base_address, info.memory.base_size, 16, 1);
    return sample(info.memory.mip_address, info.memory.mip_size, 4, h);
  }
  uint64_t h = hash_range(info.memory.base_address, info.memory.base_size, 1);
  return hash_range(info.memory.mip_address, info.memory.mip_size, h);
}

bool GuestTextureRanges(const uint32_t words[6], uint32_t ranges[4]) {
  xenos::xe_gpu_texture_fetch_t fetch = {};
  fetch.dword_0 = words[0];
  fetch.dword_1 = words[1];
  fetch.dword_2 = words[2];
  fetch.dword_3 = words[3];
  fetch.dword_4 = words[4];
  fetch.dword_5 = words[5];
  if (fetch.type != xenos::FetchConstantType::kTexture || fetch.base_address == 0) return false;
  const auto* pre = rex::graphics::FormatInfo::Get(uint32_t(fetch.format));
  if (!pre || !pre->block_width || !pre->block_height || !pre->bytes_per_block()) return false;
  rex::graphics::TextureInfo info;
  if (!rex::graphics::TextureInfo::Prepare(fetch, &info)) return false;
  ranges[0] = info.memory.base_address, ranges[1] = info.memory.base_size;
  ranges[2] = info.memory.mip_address, ranges[3] = info.memory.mip_address ? info.memory.mip_size : 0;
  return ranges[0] && ranges[1];
}

// Volume textures: base level, slice after slice. Tiled 3D textures use 32x32x4-block tiles
// (texture_util::GetTiledOffset3D); small and rare, so offsets are computed per block.
bool Decode3D(const rex::graphics::TextureInfo& info, const HostFormat& host, uint32_t swizzle,
              DecodedTexture* out) {
  const auto* fi = info.format_info();
  const uint32_t bpb = fi->bytes_per_block();
  if (!bpb || (bpb & (bpb - 1)) || !info.memory.base_address || !info.memory.base_size ||
      info.memory.base_size > (64u << 20))
    return false;
  const uint32_t bpb_log2 = uint32_t(std::countr_zero(bpb));
  const uint32_t width = info.width + 1, height = info.height + 1, depth = info.depth + 1;
  if (width > 2048 || height > 2048 || depth > 2048) return false;
  const uint32_t bw = fi->block_width, bh = fi->block_height;
  const uint32_t cols = (width + bw - 1) / bw, rows = (height + bh - 1) / bh;
  const uint32_t pitch_h = info.extent.block_pitch_h, pitch_v = info.extent.block_pitch_v;
  const uint32_t src_size = info.memory.base_size;
  if ((info.memory.base_address & 0x1FFFFFFF) + uint64_t(src_size) > 0x20000000) return false;
  const uint8_t* guest = REX_KERNEL_MEMORY()->TranslatePhysical(info.memory.base_address);
  const uint32_t host_bpb = host.convert == Convert::kNone ? bpb : host.host_bytes_per_block;
  out->format = host.format;
  out->width = cols * bw;
  out->height = rows * bh;
  out->depth = depth;
  out->volume = true;
  out->mip_count = 1;
  auto& mip = out->mips[0];
  mip.offset = 0;
  mip.width = cols * bw;
  mip.height = rows * bh;
  mip.row_texels = cols * bw;
  out->data.assign(size_t(cols) * host_bpb * rows * depth, 0);
  std::vector<uint8_t> row(size_t(cols) * bpb);
  for (uint32_t z = 0; z < depth; z++) {
    for (uint32_t by = 0; by < rows; by++) {
      for (uint32_t bx = 0; bx < cols; bx++) {
        uint32_t off = info.is_tiled
                           ? uint32_t(rex::graphics::texture_util::GetTiledOffset3D(
                                 int32_t(bx), int32_t(by), int32_t(z), pitch_h, pitch_v, bpb_log2))
                           : ((z * pitch_v + by) * pitch_h + bx) * bpb;
        if (off + bpb <= src_size) std::memcpy(row.data() + size_t(bx) * bpb, guest + off, bpb);
        else std::memset(row.data() + size_t(bx) * bpb, 0, bpb);
      }
      SwapEndian(row.data(), uint32_t(row.size()), info.endianness);
      uint8_t* dst = out->data.data() + (size_t(z) * rows + by) * cols * host_bpb;
      if (host.convert == Convert::kNone) std::memcpy(dst, row.data(), row.size());
      else ConvertRow(host.convert, row.data(), dst, cols);
    }
  }
  out->swizzle = Swizzle(info.format, swizzle);
  return true;
}

// A decoded DXT1 texture without transparent texels -> B5G6R5 (half the memory of RGBA8; the
// colour came from 5:6:5 endpoints, so only the interpolated texels lose a little precision).
static void PackOpaqueTo565(DecodedTexture* t) {
  if (!kDecodeBC || t->format != RenderFormat::R8G8B8A8_UNORM) return;
  for (uint32_t m = 0; m < t->mip_count; m++) {  // (not the zero padding between mips)
    const uint8_t* p = t->data.data() + t->mips[m].offset;
    const size_t bytes = size_t(t->mips[m].row_texels) * t->mips[m].height * 4;
    for (size_t i = 3; i < bytes; i += 4)
      if (p[i] != 255) return;  // punch-through alpha: stays RGBA8
  }
  std::vector<uint8_t> packed;
  DecodedTexture::Mip mips[16];
  for (uint32_t m = 0; m < t->mip_count; m++) {
    const auto& src = t->mips[m];
    mips[m] = src;
    mips[m].offset = uint32_t((packed.size() + 15) & ~size_t(15));
    const size_t texels = size_t(src.row_texels) * src.height;
    packed.resize(mips[m].offset + texels * 2);
    const uint8_t* in = t->data.data() + src.offset;
    uint8_t* o = packed.data() + mips[m].offset;
    for (size_t i = 0; i < texels; i++) {
      const uint16_t v = uint16_t((in[4 * i + 2] >> 3) << 11 | (in[4 * i + 1] >> 2) << 5 | (in[4 * i] >> 3));
      std::memcpy(o + 2 * i, &v, 2);
    }
  }
  std::memcpy(t->mips, mips, sizeof(mips));
  t->data = std::move(packed);
  t->format = RenderFormat::B5G6R5_UNORM;
}

bool DecodeGuestTexture(const uint32_t words[6], DecodedTexture* out) {
  xenos::xe_gpu_texture_fetch_t fetch = {};
  fetch.dword_0 = words[0];
  fetch.dword_1 = words[1];
  fetch.dword_2 = words[2];
  fetch.dword_3 = words[3];
  fetch.dword_4 = words[4];
  fetch.dword_5 = words[5];
  if (fetch.type != xenos::FetchConstantType::kTexture || fetch.base_address == 0) return false;
  // A garbage fetch constant can name a format whose table entry is all zeros, and Prepare
  // divides by its block size.
  const auto* pre = rex::graphics::FormatInfo::Get(uint32_t(fetch.format));
  if (!pre || !pre->block_width || !pre->block_height || !pre->bytes_per_block()) return false;
  rex::graphics::TextureInfo info;
  if (!rex::graphics::TextureInfo::Prepare(fetch, &info)) return false;
  HostFormat host;
  if (!GetHostFormat(info.format, &host)) return false;
  if (info.dimension == xenos::DataDimension::k3D) return Decode3D(info, host, fetch.swizzle, out);
  if (info.dimension != xenos::DataDimension::k2DOrStacked || info.is_stacked || info.width >= 8192 ||
      info.height >= 8192 || info.memory.base_address == 0 || info.memory.base_size == 0 ||
      info.memory.base_size > (64u << 20))
    return false;

  const auto* fi = info.format_info();
  const uint32_t bpb = fi->bytes_per_block();
  if (!bpb || (bpb & (bpb - 1))) return false;
  const uint32_t bpb_log2 = uint32_t(std::countr_zero(bpb));
  const uint32_t width = info.width + 1, height = info.height + 1;
  const uint32_t bw = fi->block_width, bh = fi->block_height;
  const bool pow2 = !(width & (width - 1)) && !(height & (height - 1));

  uint32_t mip_count = 1;
  if (pow2 && info.memory.mip_address) {
    uint32_t avail = std::min(info.mip_levels(), info.GetMaxMipLevels());
    while (mip_count < avail && (width >> mip_count) >= bw && (height >> mip_count) >= bh) {
      uint32_t ox, oy;
      if (!info.GetMipLocation(mip_count, &ox, &oy, true)) break;
      mip_count++;
    }
  }

  auto* mem = REX_KERNEL_MEMORY();
  out->format = host.format;
  out->width = ((width + bw - 1) / bw) * bw;
  out->height = ((height + bh - 1) / bh) * bh;
  out->mip_count = mip_count;
  out->data.clear();
  std::vector<uint8_t> row, converted;
  for (uint32_t m = 0; m < mip_count; m++) {
    uint32_t ox = 0, oy = 0;
    // Mip 0 goes through GetMipLocation too: textures <= 16 texels store their base level packed
    // inside a 32x32 tile at a block offset.
    uint32_t addr = info.GetMipLocation(m, &ox, &oy, true);
    auto ext = info.GetMipExtent(m, true);
    uint32_t pitch_blocks = m == 0 ? info.extent.block_pitch_h : ext.block_pitch_h;
    uint32_t mw = std::max(width >> m, 1u), mh = std::max(height >> m, 1u);
    uint32_t cols = (mw + bw - 1) / bw, rows = (mh + bh - 1) / bh;
    uint32_t src_size = m == 0 ? info.memory.base_size : ext.all_blocks() * bpb;
    if (info.is_tiled)
      src_size = std::max(src_size, rex::graphics::texture_util::GetTiledAddressUpperBound2D(
                                        cols + ox, rows + oy, pitch_blocks, bpb_log2));
    if ((addr & 0x1FFFFFFF) + uint64_t(src_size) > 0x20000000) return false;
    const uint8_t* guest = mem->TranslatePhysical(addr);

    const uint32_t host_bpb = host.convert == Convert::kNone ? bpb : host.host_bytes_per_block;
    auto& mip = out->mips[m];
    mip.width = std::max(cols * bw, 1u);
    mip.height = std::max(rows * bh, 1u);
    mip.row_texels = cols * bw;
    mip.offset = uint32_t((out->data.size() + 15) & ~size_t(15));
    out->data.resize(mip.offset + size_t(cols) * host_bpb * rows);
    const uint32_t row_bytes = cols * bpb;
    const uint32_t run_blocks = std::clamp(16u >> bpb_log2, 1u, 8u);
    row.resize(row_bytes);
    converted.resize(size_t(cols) * host_bpb);
    for (uint32_t by = 0; by < rows; by++) {
      if (!info.is_tiled) {
        uint32_t off = ((by + oy) * pitch_blocks + ox) * bpb;
        if (off + row_bytes <= src_size) std::memcpy(row.data(), guest + off, row_bytes);
        else std::memset(row.data(), 0, row_bytes);
      } else {
        // Within a row, runs of run_blocks aligned blocks map to contiguous tiled offsets.
        for (uint32_t bx = 0; bx < cols;) {
          uint32_t x = bx + ox;
          uint32_t run = std::min(cols - bx, run_blocks - (x & (run_blocks - 1)));
          uint32_t off = uint32_t(rex::graphics::texture_util::GetTiledOffset2D(int32_t(x), int32_t(by + oy),
                                                                                pitch_blocks, bpb_log2));
          uint32_t bytes = run * bpb;
          if (off + bytes <= src_size) std::memcpy(row.data() + size_t(bx) * bpb, guest + off, bytes);
          else std::memset(row.data() + size_t(bx) * bpb, 0, bytes);
          bx += run;
        }
      }
      SwapEndian(row.data(), row_bytes, info.endianness);
      uint8_t* dst = out->data.data() + mip.offset + size_t(by) * cols * host_bpb;
      if (host.convert == Convert::kNone) {
        std::memcpy(dst, row.data(), row_bytes);
      } else {
        ConvertRow(host.convert, row.data(), dst, cols);
      }
    }
  }
  out->swizzle = Swizzle(info.format, fetch.swizzle);
#ifdef __ANDROID__
  // Adreno cannot sample R16G16B16A16_UNORM at all (no SAMPLED_IMAGE format feature): store the
  // same 0..1 values as SNORM (sampled and filtered there), 15 bits of the 16.
  if (out->format == RenderFormat::R16G16B16A16_UNORM) {
    auto* v = reinterpret_cast<uint16_t*>(out->data.data());
    for (size_t i = 0, n = out->data.size() / 2; i < n; i++) v[i] >>= 1;
    out->format = RenderFormat::R16G16B16A16_SNORM;
  }
#endif
  if (host.convert == Convert::kBC1) PackOpaqueTo565(out);
  return true;
}

}  // namespace gtav::native
