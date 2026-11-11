// gtav - Xenos shader capture: container -> hash, and guest shader object -> hash.
//
// The XDK shader create functions take a shader container (flags 0x102A11xx, then virtual and
// physical sizes) in r3 and return the new D3D shader object in r3. Each container is hashed
// the way XenosRecomp keys its shader cache (XXH3-64 over virtualSize + physicalSize bytes),
// so a guest shader object bound at draw time maps to its precompiled host shader.
// --gtav_native_shader_dump=DIR also writes every container to DIR/<hash>.bin, which is the
// input for the offline XenosRecomp pass.

#include "shaders.h"
#include "shader_compiler.h"

#include <rex/cvar.h>
#include <rex/kernel.h>
#include <rex/system.h>
#include <rex/hook.h>
#include <rex/logging.h>

#define XXH_INLINE_ALL
#include <xxhash.h>

#include <atomic>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

REXCVAR_DEFINE_STRING(gtav_native_shader_dump, "", "GTAV",
                      "Directory to dump every Xenos shader container the game creates");

// SDK (librexgpu): float constant registers a shader's ucode can read (256 if indexed).
extern "C" bool rex_shader_float_constant_bitmap(bool pixel, const uint32_t* ucode_dwords, uint32_t dword_count,
                                                 uint64_t out[4]);

namespace gtav::native {
namespace {

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

std::mutex g_mutex;
std::unordered_map<uint32_t, uint64_t> g_object_hash;  // guest shader object -> container hash
std::unordered_map<uint32_t, uint64_t> g_object_ucode_hash;  // -> microcode hash (Xenia's key)
std::unordered_map<uint32_t, const ConstantUse*> g_object_consts;  // -> float constant registers it reads
// Interned by microcode hash and never freed: lookups hand out plain pointers.
std::unordered_map<uint64_t, std::unique_ptr<ConstantUse>> g_constant_uses;
const ConstantUse g_all_constants;

const ConstantUse* MakeConstantUse(bool pixel, const uint8_t* ucode, uint32_t size, uint64_t ucode_hash) {
  auto& slot = g_constant_uses[ucode_hash ^ (pixel ? 1 : 0)];
  if (slot) return slot.get();
  uint64_t bits[4];
  auto use = std::make_unique<ConstantUse>();
  if (size >= 4 && rex_shader_float_constant_bitmap(pixel, reinterpret_cast<const uint32_t*>(ucode), size / 4, bits)) {
    use->runs.clear();
    use->count = 0;
    for (uint32_t r = 0; r < 256; r++) {
      if (!((bits[r >> 6] >> (r & 63)) & 1)) continue;
      // Gaps of up to 2 registers are copied too: fewer, longer copies.
      if (!use->runs.empty() && r - (use->runs.back().first + use->runs.back().count) <= 2)
        use->runs.back().count = uint16_t(r + 1 - use->runs.back().first);
      else
        use->runs.push_back({uint16_t(r), 1});
      use->count = r + 1;
    }
  }
  slot = std::move(use);
  return slot.get();
}
std::unordered_map<uint64_t, uint64_t> g_ucode_container;  // microcode hash -> container hash
std::atomic<uint32_t> g_created{0}, g_dumped{0};
// Bumped by every create: invalidates the per-thread lookup caches below.
std::atomic<uint32_t> g_generation{1};

// Draws look up the same few shader objects thousands of times a frame; a mutex + map lookup
// per call (three per draw) showed up in the game thread's profile.
struct CachedLookup {
  uint32_t object = 0, generation = 0;
  uint64_t hash = 0, ucode_hash = 0;
  const ConstantUse* consts = nullptr;
};
const CachedLookup& Lookup(uint32_t guest_object) {
  thread_local CachedLookup cache[64];
  CachedLookup& e = cache[(guest_object >> 4) & 63];
  uint32_t generation = g_generation.load(std::memory_order_acquire);
  if (e.object == guest_object && e.generation == generation) return e;
  std::lock_guard lock(g_mutex);
  auto it = g_object_hash.find(guest_object);
  auto ut = g_object_ucode_hash.find(guest_object);
  auto ct = g_object_consts.find(guest_object);
  e = {guest_object, generation, it == g_object_hash.end() ? 0 : it->second,
       ut == g_object_ucode_hash.end() ? 0 : ut->second, ct == g_object_consts.end() ? &g_all_constants : ct->second};
  return e;
}

void OnCreate(const uint8_t* base, uint32_t container, uint32_t object, bool pixel) {
  if (!object || !container) return;
  const uint8_t* c = REX_KERNEL_MEMORY()->TranslateVirtual(container);
  uint32_t flags = be32(c), size = be32(c + 4) + be32(c + 8);
  if ((flags & 0xFFFFFF00) != 0x102A1100 || size > (16u << 20)) return;
  uint64_t hash = XXH3_64bits(c, size);
  // Xenia keys shaders by XXH3 of the raw (big-endian) microcode: the container's physical part,
  // located by the shader header (physical offset, size) at container + shaderOffset.
  uint32_t header = be32(c + 0x18);
  uint32_t ucode_offset = be32(c + 4) + be32(c + header), ucode_size = be32(c + header + 4);
  uint64_t ucode_hash = ucode_offset + ucode_size <= size ? XXH3_64bits(c + ucode_offset, ucode_size) : 0;
  {
    std::lock_guard lock(g_mutex);
    g_object_hash[object] = hash;
    g_object_ucode_hash[object] = ucode_hash;
    g_object_consts[object] = ucode_offset + ucode_size <= size
                                  ? MakeConstantUse(pixel, c + ucode_offset, ucode_size, ucode_hash)
                                  : &g_all_constants;
    g_ucode_container[ucode_hash] = hash;
    g_generation.fetch_add(1, std::memory_order_release);
  }
  OnShaderCreated(c, size, hash, pixel);
  uint32_t created = g_created.fetch_add(1, std::memory_order_relaxed);
  if (created < 4) {
    // Layout discovery: which object field points at the (physical) microcode copy?
    const uint8_t* phys = c + be32(c + 4);
    auto* mem = REX_KERNEL_MEMORY();
    const uint8_t* obj = mem->TranslateVirtual(object);
    for (uint32_t off = 0; off < 872; off += 4) {
      uint32_t v = be32(obj + off);
      if (v < 0x1000) continue;
      const uint8_t* cand = (v >= 0x80000000) ? mem->TranslateVirtual(v) : mem->TranslatePhysical(v);
      if (v < 0x20000000 || v >= 0xA0000000) {
        if (std::memcmp(cand, phys, 32) == 0)
          REXLOG_INFO("[native-probe] {} obj {:08X}: microcode ptr at +{} = {:08X} (phys size {})",
                      pixel ? "ps" : "vs", object, off, v, be32(c + 8));
      }
    }
  }
  const std::string& dir = REXCVAR_GET(gtav_native_shader_dump);
  if (dir.empty()) return;
  auto path = std::filesystem::path(dir) / fmt::format("{}_{:016X}.bin", pixel ? "ps" : "vs", hash);
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) return;
  std::filesystem::create_directories(dir, ec);
  if (FILE* f = std::fopen(path.string().c_str(), "wb")) {
    std::fwrite(c, 1, size, f);
    std::fclose(f);
    uint32_t n = g_dumped.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n % 250 == 0) REXLOG_INFO("[native] dumped {} shader containers", n);
  }
}

}  // namespace

uint64_t ShaderHash(uint32_t guest_object) { return Lookup(guest_object).hash; }

uint64_t ShaderUcodeHash(uint32_t guest_object) { return Lookup(guest_object).ucode_hash; }

const ConstantUse& ShaderConstants(uint32_t guest_object) { return *Lookup(guest_object).consts; }

uint64_t ContainerForUcode(uint64_t ucode_hash) {
  std::lock_guard lock(g_mutex);
  auto it = g_ucode_container.find(ucode_hash);
  return it == g_ucode_container.end() ? 0 : it->second;
}

}  // namespace gtav::native

// Shader create functions (which one is vertex vs pixel is confirmed from the dumped
// containers; the guess follows the call order in RAGE's program loader).
REX_EXTERN(__imp__sub_83813218);
REX_HOOK_RAW(sub_83813218) {
  uint32_t container = ctx.r3.u32;
  __imp__sub_83813218(ctx, base);
  gtav::native::OnCreate(base, container, ctx.r3.u32, false);
}

REX_EXTERN(__imp__sub_83813030);
REX_HOOK_RAW(sub_83813030) {
  uint32_t container = ctx.r3.u32;
  __imp__sub_83813030(ctx, base);
  gtav::native::OnCreate(base, container, ctx.r3.u32, true);
}
