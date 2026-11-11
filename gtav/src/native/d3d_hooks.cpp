// gtav - every XDK D3D hook used by the native renderer (shadow mode: observe, then call
// through). Addresses and argument layouts: d3d_map.h / docs/d3d_map.md.

#include "capture.h"
#include "d3d_map.h"
#include "renderer.h"
#include "stats.h"

#include <rex/cvar.h>
#include <rex/hook.h>

#include <chrono>
#include <cstring>

namespace gtav::native {
void ProbeFrame();
}

using namespace gtav::native;

REXCVAR_DECLARE(std::string, gtav_native);

#define GTAV_COUNT_HOOK(addr, counter) \
  REX_EXTERN(__imp__sub_##addr);       \
  REX_HOOK_RAW(sub_##addr) {           \
    StatCount(counter);                \
    __imp__sub_##addr(ctx, base);      \
  }

GTAV_COUNT_HOOK(83818300, kStatRs)

// D3DQuery_Issue(query, flags) (the D3D function itself; GTA reaches it through two tail-calling
// wrappers).
REX_EXTERN(__imp__sub_83815FB0);
REX_HOOK_RAW(sub_83815FB0) {
  OnQueryIssue(ctx.r3.u32, ctx.r4.u32);
  __imp__sub_83815FB0(ctx, base);
}

// GTA's query poll 832BA500(query, &samples) -> ready (calls D3DQuery_GetData without flush). With
// --gtav_native=on the command processor's count is a fake; the native renderer's count replaces
// it, and the query stays "not ready" until the native frame that drew it completed.
REX_EXTERN(__imp__sub_832BA500);
REX_HOOK_RAW(sub_832BA500) {
  uint32_t query = ctx.r3.u32, out = ctx.r4.u32;
  __imp__sub_832BA500(ctx, base);
  static const bool on = REXCVAR_GET(gtav_native) == "on";
  if (!on || !out) return;
  uint32_t samples = 0;
  switch (OverrideQueryResult(query, &samples)) {
    case QueryOverride::kOriginal: return;
    case QueryOverride::kReady: ctx.r3.u64 = 1; break;
    case QueryOverride::kNotReady: ctx.r3.u64 = 0; break;
  }
  uint32_t be = __builtin_bswap32(samples);
  std::memcpy(base + out, &be, 4);
}

// DrawIndexedVertices(dev, ...) / DrawVertices(dev, ...)
REX_EXTERN(__imp__sub_83811030);
REX_HOOK_RAW(sub_83811030) {
  StatCount(kStatDrawIndexed);
  OnDrawIndexed(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32);
  uint32_t device = ctx.r3.u32;
  ProbeDrawBefore(device);
  __imp__sub_83811030(ctx, base);
  ProbeDrawAfter(device);
}

REX_EXTERN(__imp__sub_83810C18);
REX_HOOK_RAW(sub_83810C18) {
  StatCount(kStatDraw);
  OnDraw(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32);
  __imp__sub_83810C18(ctx, base);
}

// 838087C8(dev, ...): runs recorded D3D command buffers (writes PM4 INDIRECT_BUFFER packets, the
// only game-called D3D function that does). The draws inside them were captured while recording.
REX_EXTERN(__imp__sub_838087C8);
REX_HOOK_RAW(sub_838087C8) {
  uint32_t device = ctx.r3.u32;
  auto write = [&] { return __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + device + d3d::kDevCmdWrite)); };
  uint32_t before = device ? write() : 0;
  __imp__sub_838087C8(ctx, base);
  if (device) OnCommandBufferRun(device, before, write());
}
// SetPixelShader(dev, shader) / SetVertexShader(dev, shader) / SetIndices(dev, ib).
// (Skate 3's labels for the first two were swapped; the bound containers' type flags decide.)
REX_EXTERN(__imp__sub_83812190);
REX_HOOK_RAW(sub_83812190) {
  StatCount(kStatPs);
  OnSetPixelShader(ctx.r3.u32, ctx.r4.u32);
  __imp__sub_83812190(ctx, base);
}

REX_EXTERN(__imp__sub_83812398);
REX_HOOK_RAW(sub_83812398) {
  StatCount(kStatVs);
  OnSetVertexShader(ctx.r3.u32, ctx.r4.u32);
  __imp__sub_83812398(ctx, base);
}

REX_EXTERN(__imp__sub_83806E70);
REX_HOOK_RAW(sub_83806E70) {
  StatCount(kStatIndices);
  OnSetIndices(ctx.r3.u32, ctx.r4.u32);
  __imp__sub_83806E70(ctx, base);
}

// BeginVertices(dev, primitive, vertex count, stride) -> r3 = vertex write pointer;
// EndVertices(dev) after the CPU wrote them.
REX_EXTERN(__imp__sub_83810218);
REX_HOOK_RAW(sub_83810218) {
  StatCount(kStatBeginVerts);
  uint32_t device = ctx.r3.u32, primitive = ctx.r4.u32, count = ctx.r5.u32, stride = ctx.r6.u32;
  __imp__sub_83810218(ctx, base);
  OnBeginVertices(device, primitive, count, stride, ctx.r3.u32);
}

// BeginIndexedVertices(dev, primitive, base vertex, vertex count, index count, index format, stride,
// &indices (r10), &vertices (stack)): the pointers land at dev+13848/13852.
REX_EXTERN(__imp__sub_838106D8);
REX_HOOK_RAW(sub_838106D8) {
  StatCount(kStatDrawOther);
  uint32_t device = ctx.r3.u32, primitive = ctx.r4.u32, vertices = ctx.r6.u32, indices = ctx.r7.u32,
           format = ctx.r8.u32, stride = ctx.r9.u32;
  int32_t base_vertex = ctx.r5.s32;
  __imp__sub_838106D8(ctx, base);
  OnBeginIndexedVertices(device, primitive, base_vertex, vertices, indices, format, stride);
}

REX_EXTERN(__imp__sub_83810208);
REX_HOOK_RAW(sub_83810208) {
  OnEndVertices();
  __imp__sub_83810208(ctx, base);
}

// Inline ALU constant load (dev, bank 0 VS / 1 PS, start, count) -> r3 = data pointer.
REX_EXTERN(__imp__sub_83813580);
REX_HOOK_RAW(sub_83813580) {
  uint32_t bank = ctx.r4.u32, start = ctx.r5.u32, count = ctx.r6.u32;
  __imp__sub_83813580(ctx, base);
  OnInlineConstants(bank, start, count, ctx.r3.u32);
}


// SetPending_AluConstants(dev, dirty mask (64-bit, MSB = registers 0-3), base 0x4000/0x4400, shadow):
// flushes shadow registers to the GPU, replacing inline-loaded values for those registers.
REX_EXTERN(__imp__sub_83818698);
REX_HOOK_RAW(sub_83818698) {
  StatCount(kStatAlu);
  OnFlushConstants(ctx.r5.u32 == 0x4400 ? 1 : 0, ctx.r4.u64);
  __imp__sub_83818698(ctx, base);
}

// SetVertexShaderConstantF / SetPixelShaderConstantF (dev, start, data, count): shadow writes.
REX_EXTERN(__imp__sub_83811EF0);
REX_HOOK_RAW(sub_83811EF0) {
  OnSetConstants(0, ctx.r4.u32, ctx.r6.u32);
  __imp__sub_83811EF0(ctx, base);
}

REX_EXTERN(__imp__sub_83811FC8);
REX_HOOK_RAW(sub_83811FC8) {
  OnSetConstants(1, ctx.r4.u32, ctx.r6.u32);
  __imp__sub_83811FC8(ctx, base);
}

// SetStreamSource(dev, stream, vb, offset, stride)
REX_EXTERN(__imp__sub_83806D50);
REX_HOOK_RAW(sub_83806D50) {
  StatCount(kStatStreams);
  OnSetStreamSource(ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32);
  __imp__sub_83806D50(ctx, base);
}

// D3DDevice_Clear(dev, count, rects, flags, color, z (f1), stencil)
REX_EXTERN(__imp__sub_8380BCD8);
REX_HOOK_RAW(sub_8380BCD8) {
  // The register shadow is read after the call: Clear first applies pending render-target state
  // (surface pitch/bases). Read before, a clear of the 640-pitch shadow cascades at EDRAM 000 hit
  // the 1280-pitch scene depth there, and the far-plane fog meshes then covered the whole screen.
  uint32_t device = ctx.r3.u32, count = ctx.r4.u32, rects = ctx.r5.u32, flags = ctx.r6.u32, color = ctx.r7.u32,
           stencil = ctx.r9.u32;
  float z = float(ctx.f1.f64);  // Z takes r8's slot
  OnClearBegin(device);
  __imp__sub_8380BCD8(ctx, base);
  OnClear(device, flags, color, z, stencil, rects ? count : 0, rects);
}

// D3DDevice_Resolve(dev, flags, src rect, dest texture, dest point, level, slice, clear color (r10),
// clear Z (f1), clear stencil, parameters)
REX_EXTERN(__imp__sub_838045E0);
REX_HOOK_RAW(sub_838045E0) {
  OnResolve(ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r10.u32, float(ctx.f1.f64), ctx.r8.u32,
            ctx.r9.u32);
  __imp__sub_838045E0(ctx, base);
}

// D3DDevice_Swap(dev, ...)
REX_EXTERN(__imp__sub_838146B8);
REX_HOOK_RAW(sub_838146B8) {
  ProbeFrame();
  OnCaptureSwap(ctx.r4.u32);
  if (!StatsEnabled()) {
    __imp__sub_838146B8(ctx, base);
    return;
  }
  uint32_t device = ctx.r3.u32;
  auto begin = std::chrono::steady_clock::now();
  __imp__sub_838146B8(ctx, base);
  StatsSwap(begin, std::chrono::steady_clock::now(), device);
}
