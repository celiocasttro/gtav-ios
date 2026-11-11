// gtav - guest D3D state tracking for the native renderer.
//
// The D3D hooks report bound objects (index buffer, shaders) and draws here. Everything else a
// draw needs is read from the device's register shadow (see d3d_map.h).

#pragma once

#include <cstdint>

namespace gtav::native {

void OnSetIndices(uint32_t device, uint32_t index_buffer);
void OnSetStreamSource(uint32_t device, uint32_t stream, uint32_t vb, uint32_t offset, uint32_t stride);
// 83813580(dev, bank, start, count) -> r3 = where the caller writes count float4 constants.
void OnInlineConstants(uint32_t bank, uint32_t start, uint32_t count, uint32_t data);
// Set{Vertex,Pixel}ShaderConstantF(dev, start, data, count): the shadow is current again.
void OnSetConstants(uint32_t bank, uint32_t start, uint32_t count);
// SetPending_AluConstants: the shadow registers in dirty_mask (4 per bit, MSB first) reach the GPU.
void OnFlushConstants(uint32_t bank, uint64_t dirty_mask);
void OnSetVertexShader(uint32_t device, uint32_t shader);
void OnSetPixelShader(uint32_t device, uint32_t shader);

// D3DDevice_DrawIndexedVertices / DrawVertices arguments (r4..r7 as the guest passed them).
void OnDrawIndexed(uint8_t* base, uint32_t device, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7);
void OnDraw(uint8_t* base, uint32_t device, uint32_t a4, uint32_t a5, uint32_t a6);


void OnBeginVertices(uint32_t device, uint32_t primitive, uint32_t count, uint32_t stride, uint32_t data);
void OnEndVertices();
void OnBeginIndexedVertices(uint32_t device, uint32_t primitive, int32_t base_vertex, uint32_t vertex_count,
                            uint32_t index_count, uint32_t index_format, uint32_t stride);
void OnClearBegin(uint32_t device);
void ProbeDrawBefore(uint32_t device);  // diagnostic: registers the D3D draw call flushes
void ProbeDrawAfter(uint32_t device);  // before the D3D call (inline vertices still pending)
void OnClear(uint32_t device, uint32_t flags, uint32_t color, float z, uint32_t stencil, uint32_t rect_count,
             uint32_t rects);
void OnResolve(uint32_t device, uint32_t flags, uint32_t rect, uint32_t dest_texture, uint32_t dest_point,
               uint32_t clear_color, float clear_z, uint32_t level, uint32_t face);

// 838087C8 (runs D3D command buffers): the device's PM4 write pointer before and after the call;
// the INDIRECT_BUFFER packets it wrote name the recorded buffers that execute now.
void OnCommandBufferRun(uint32_t device, uint32_t write_before, uint32_t write_after);

// D3DQuery Issue(query, flags): occlusion query BEGIN / END, recorded in the frame.
void OnQueryIssue(uint32_t query, uint32_t flags);
// Render thread: the samples a recorded query passed, once its frame's fence passed.
void PublishQueryResult(uint32_t query, uint32_t serial, uint64_t samples);
// Guest GetData on `query` (--gtav_native=on): the native count when the last END has one.
enum class QueryOverride { kOriginal, kReady, kNotReady };
QueryOverride OverrideQueryResult(uint32_t query, uint32_t* samples);

// Guest frame boundary (before the guest's Swap runs): hands the frame to the renderer.
void OnCaptureSwap(uint32_t front_buffer);
uint64_t GuestFrame();
// Render thread, before recording frame f: moves the fence-time copies of its draws into it.
struct FramePacket;
void ApplyFenceCopies(FramePacket& f);
// Render thread, after recording frame `frame` (its guest memory has been read).
void FenceCopiesRecorded(uint64_t frame);

// Render thread: draws [0, count) of the frame being recorded have read their guest data.
void DrawsRecorded(uint32_t count);
// Command processor thread: releases the GPU fence values held back (before its swap).
void FenceFlush();

}  // namespace gtav::native
