// gtav - addresses of the statically linked XDK D3D runtime in the TU27 image.
//
// Found by fuzzy fingerprint against Skate 3's named D3D functions (tools/native/fnsim.py;
// the two titles ship nearly the same XDK D3D library). Score = similarity to the Skate 3
// function; 1.00 means identical modulo immediates. See docs/d3d_map.md.

#pragma once

#include <cstdint>

namespace gtav::native::d3d {

constexpr uint32_t kSwap = 0x838146B8;                   // 0.61, the VdSwap caller
constexpr uint32_t kDrawIndexedVertices = 0x83811030;    // 0.91
constexpr uint32_t kDrawVertices = 0x83810C18;           // 0.91
constexpr uint32_t kBeginVertices = 0x83810218;          // 0.86, inline vertex ring
constexpr uint32_t kBeginIndexedVertices = 0x838106D8;   // UP indexed draw: CPU fills ring buffers after
constexpr uint32_t kSetIndices = 0x83806E70;             // 1.00
constexpr uint32_t kSetStreamSource = 0x83806D50;        // 0.77
constexpr uint32_t kSetPixelShader = 0x83812190;         // 1.00 (Skate label swapped; container flags)
constexpr uint32_t kSetVertexShader = 0x83812398;        // 1.00
constexpr uint32_t kSetPendingAluConstants = 0x83818698; // 1.00
constexpr uint32_t kSetPendingRenderStates = 0x83818300; // 1.00
constexpr uint32_t kSetScissorRect = 0x838072C8;         // 1.00
constexpr uint32_t kSetViewport = 0x8380DC68;            // 0.54, unconfirmed

// D3D::CDevice layout (offsets shared with Skate 3's XDK).
constexpr uint32_t kDevFetchConstants = 0x480;  // 32 x 6 dwords (xe_gpu_texture_fetch_t)
constexpr uint32_t kDevVsConstants = 1920;      // ALU constant shadow, bank 0x4000
constexpr uint32_t kDevPsConstants = 6016;      // ALU constant shadow, bank 0x4400
constexpr uint32_t kDevCmdWrite = 0x30;         // PM4 command buffer write pointer
constexpr uint32_t kDevCmdLimit = 0x34;
constexpr uint32_t kDevBoolConstants = 10112;   // reg 0x4900
constexpr uint32_t kDevPixelShader = 13068;   // SetPixelShader (83812190) stores the shader here
constexpr uint32_t kDevVertexShader = 13072;  // SetVertexShader (83812398) stores the shader here
constexpr uint32_t kDevVertexDeclaration = 12240;  // set by SetVertexDeclaration (838125B0)
constexpr uint32_t kDevUpVertices = 13848;  // BeginIndexedVertices: vertex data pointer
constexpr uint32_t kDevUpIndices = 13852;   // BeginIndexedVertices: index data pointer
constexpr uint32_t kDevRenderTargets = 12816;      // 4 surfaces (GetRenderTarget 83806F00)
constexpr uint32_t kDevIndexBuffer = 12812;       // current IB (DrawIndexedVertices 83811030 reads it)
constexpr uint32_t kDevDepthStencil = 12832;       // GetDepthStencilSurface 83806F48
constexpr uint32_t kDevViewport = 13024;
// Vertex declaration object: element count at +24, D3DVERTEXELEMENT9 array (12 bytes each) at +52.
constexpr uint32_t kDeclCount = 24;
constexpr uint32_t kDeclElements = 52;
// Index buffer Common flag for 32-bit indices (unverified guess; checked against draw ranges).
constexpr uint32_t kIndexBuffer32Flag = 0x80000000;
constexpr uint32_t kResolve = 0x838045E0;  // (dev, flags, src rect, dest texture, dest point, ...)           // X, Y, W, H, MinZ, MaxZ floats (GetViewport 83806D00)
// Register shadow blocks: register `reg` of a block lives at dev + block + 4 * (reg - base).
constexpr uint32_t kDevRegs2000 = 10368;
constexpr uint32_t kDevRegs2100 = 10444;
constexpr uint32_t kDevRegs2180 = 10528;
constexpr uint32_t kDevRegs2200 = 10548;
constexpr uint32_t kDevRegs2280 = 10596;
constexpr uint32_t kDevRegs2300 = 10680;
constexpr uint32_t kDevRegs2380 = 10832;

}  // namespace gtav::native::d3d
