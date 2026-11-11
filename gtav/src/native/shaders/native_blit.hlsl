// gtav - native renderer: present blit (guest front buffer -> swapchain). Uses the backend's
// pipeline layout: texture heap in set 0, samplers in set 3, descriptor indices as push constants.

struct PushConstants {
    uint TextureIndex;
    uint SamplerIndex;
    int2 SourceMinusDest;  // resolves: source texel = destination pixel + this
    float Scale;           // color resolves: value conversion between source and destination
    uint Mode;             // color resolves: 1 = re-encode float channels as 7e3 bit patterns,
                           // 3 = 2x nearest upscale (4x MSAA EDRAM read as a 1x surface)
};

[[vk::push_constant]] ConstantBuffer<PushConstants> g_Push;
Texture2D<float4> g_Textures[] : register(t0, space0);
Texture2D<uint4> g_UintTextures[] : register(t0, space0);  // same heap, integer views (stencil)
SamplerState g_Samplers[] : register(s0, space3);

void VSMain(uint id : SV_VertexID, out float4 position : SV_Position, out float2 uv : TEXCOORD0) {
    uv = float2((id << 1) & 2, id & 2);
    position = float4(uv * 2.0 - 1.0, 0.0, 1.0);  // Vulkan: y = -1 is the top of the target
}

float4 PSMain(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    return float4(g_Textures[g_Push.TextureIndex].SampleLevel(g_Samplers[g_Push.SamplerIndex], uv, 0).rgb, 1.0);
}

// Depth resolve: copy depth texels (depth-aspect view of the depth target) into an R32_FLOAT
// target; the resolve rectangle is the viewport, SourceMinusDest maps it back to the source.
float PSDepth(float4 position : SV_Position) : SV_Target {
    int2 source = int2(position.xy) + g_Push.SourceMinusDest;
    float d = g_Textures[g_Push.TextureIndex].Load(int3(source, 0)).r;
    if (g_Push.Mode == 2) {
        // D24FS8 source: the Xbox stores depth as 20e4 (20-bit mantissa, truncated, as Xenia's
        // default), and GTA's shadow bias is tuned to it; the host keeps 32-bit floats.
        if (d >= 6.103515625e-5)  // 2^-14, smallest normal 20e4
            d = asfloat(asuint(min(d, 1.99999988)) & ~7u);
        else
            d = floor(d * 17179869184.0) / 17179869184.0;  // denormal step 2^-34
    }
    return d;
}

// Color resolve with format conversion: the destination target has the host format of the guest
// destination texture; Scale converts between the source's and destination's value encodings.
// Xenos 7e3 (3-bit exponent, 7-bit mantissa, [0, 31.875]) from float, as Xenia converts it.
uint Float32To7e3(float f) {
    if (!(f > 0.0)) return 0;
    if (f >= 31.875) return 0x3FF;
    uint u = asuint(f);
    if (u < 0x3E800000) {
        u = (0x800000 | (u & 0x7FFFFF)) >> (125 - (u >> 23));
    } else {
        u += 0xC2000000;
    }
    return ((u + 0x7FFF + ((u >> 16) & 1)) >> 16) & 0x3FF;
}

float4 PSResolve(float4 position : SV_Position) : SV_Target {
    int2 source = (g_Push.Mode == 3 ? int2(position.xy) / 2 : int2(position.xy)) + g_Push.SourceMinusDest;
    float4 v = g_Textures[g_Push.TextureIndex].Load(int3(source, 0)) * g_Push.Scale;
    if (g_Push.Mode == 1) {
        // EDRAM holds 2_10_10_10_FLOAT and the guest resolves it as fixed 2_10_10_10: the raw
        // 7e3 bit patterns land in the texture as 10-bit unorm values.
        return float4(Float32To7e3(v.r) / 1023.0, Float32To7e3(v.g) / 1023.0, Float32To7e3(v.b) / 1023.0, saturate(v.a));
    }
    return v;
}

// Xenos 20e4 (k_24_8_FLOAT depth: 4-bit exponent, 20-bit mantissa), as Xenia converts it.
uint Float32To20e4(float f) {
    if (!(f > 0.0)) return 0;
    uint u = asuint(f);
    if (u >= 0x3FFFFFF8) return 0xFFFFFF;
    if (u < 0x38800000) {
        u = (0x800000 | (u & 0x7FFFFF)) >> min(113 - (u >> 23), 24);
    } else {
        u += 0xC8000000;
    }
    u += 3 + ((u >> 3) & 1);
    return (u >> 3) & 0xFFFFFF;
}

// Depth resolve as the raw D24S8 word, for guests that fetch it through an 8888 view: the
// texel components (x = stencil, y..w = depth bits 0-7, 8-15, 16-23) are written already
// swizzled ZYXW, the swizzle GTA V samples it with. Mode 1: float24 depth (k_24_8_FLOAT).
float4 PSDepthPack(float4 position : SV_Position) : SV_Target {
    int2 source = int2(position.xy) + g_Push.SourceMinusDest;
    float depth = g_Textures[g_Push.TextureIndex].Load(int3(source, 0)).r;
    uint stencil = g_UintTextures[g_Push.SamplerIndex].Load(int3(source, 0)).r & 0xFF;
    uint d = g_Push.Mode == 1 ? Float32To20e4(depth) : uint(saturate(depth) * 16777215.0 + 0.5);
    return float4((d >> 8) & 0xFF, d & 0xFF, stencil, (d >> 16) & 0xFF) / 255.0;
}

// FPS counter (--gtav_native_fps): the number in TextureIndex as seven-segment digits over a dark
// box; SamplerIndex is the pixel scale, SourceMinusDest the box's top-left corner.
static const uint kDigitSegments[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

bool Segment(float2 p, float4 box) { return all(p >= box.xy) && all(p < box.zw); }

float4 PSDigits(float4 position : SV_Position) : SV_Target {
    float scale = max(float(g_Push.SamplerIndex), 1.0);
    float2 p = (position.xy - float2(g_Push.SourceMinusDest)) / scale - 4.0;  // 4px margin
    uint value = g_Push.TextureIndex;
    const uint kDigits = 4;
    const float kCell = 18.0;  // 14 wide + 4 gap
    int index = int(floor(p.x / kCell));
    if (index < 0 || index >= int(kDigits) || p.y < 0.0 || p.y >= 24.0)
        return float4(0.0, 0.0, 0.0, 0.6);
    uint divisor = 1;
    for (int i = index + 1; i < int(kDigits); i++) divisor *= 10;
    uint digit = (value / divisor) % 10;
    if (value < divisor && index != int(kDigits) - 1)
        return float4(0.0, 0.0, 0.0, 0.6);  // no leading zeros
    float2 c = float2(p.x - index * kCell, p.y);
    uint s = kDigitSegments[digit];
    bool on = ((s & 0x01) && Segment(c, float4(2, 0, 12, 3))) || ((s & 0x02) && Segment(c, float4(11, 2, 14, 12))) ||
              ((s & 0x04) && Segment(c, float4(11, 12, 14, 22))) || ((s & 0x08) && Segment(c, float4(2, 21, 12, 24))) ||
              ((s & 0x10) && Segment(c, float4(0, 12, 3, 22))) || ((s & 0x20) && Segment(c, float4(0, 2, 3, 12))) ||
              ((s & 0x40) && Segment(c, float4(2, 10.5, 12, 13.5)));
    return on ? float4(0.2, 1.0, 0.3, 1.0) : float4(0.0, 0.0, 0.0, 0.6);
}

// Tree-orb diagnostic (F11 "flat" modes): replaces a guest pixel shader with magenta in RT0, no
// textures or kill, so the picture shows where the draw rasterizes and passes depth. The shade
// varies per triangle so that per-draw snapshots still show a draw that covers an earlier one.
float4 PSDiagFlat(float4 position : SV_Position, uint primitive : SV_PrimitiveID) : SV_Target0 {
    float shade = 0.55 + 0.45 * float((primitive * 2654435761u) >> 24) / 255.0;
    return float4(shade, 0.0, shade, 1.0);
}

// Frame dumps (GTAV_NATIVE_DUMP_ALPHA=1): per-draw snapshots show the target's alpha as grey.
float4 PSAlpha(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float a = g_Textures[g_Push.TextureIndex].SampleLevel(g_Samplers[g_Push.SamplerIndex], uv, 0).a;
    return float4(a, a, a, 1.0);
}
