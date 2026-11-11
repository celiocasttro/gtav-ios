// gtav - native renderer: colour resolve inside the source's render pass (tile GPUs: MoltenVK on
// Apple). The source attachment is read under the fragment as an input attachment (Metal
// framebuffer fetch) and stored into the resolve texture, instead of ending the pass for a copy
// pass of its own. Same conversion as PSResolve (native_blit.hlsl). SOURCE: attachment index,
// set when compiling (glslangValidator -DSOURCE=n).
#version 450
#extension GL_EXT_nonuniform_qualifier : require
layout(input_attachment_index = SOURCE, set = 0, binding = 0) uniform subpassInput g_Source;
layout(set = 1, binding = 0) uniform writeonly image2D g_Dest[];
layout(push_constant) uniform Push {
    ivec2 DestMinusSource;  // destination texel = source pixel + this
    float Scale;
    uint Mode;  // 1: float channels re-encoded as 7e3 bit patterns
    uint DestIndex;
} g_Push;

// Xenos 7e3 (3-bit exponent, 7-bit mantissa, [0, 31.875]) from float, as Xenia converts it.
uint Float32To7e3(float f) {
    if (!(f > 0.0)) return 0u;
    if (f >= 31.875) return 0x3FFu;
    uint u = floatBitsToUint(f);
    if (u < 0x3E800000u) {
        u = (0x800000u | (u & 0x7FFFFFu)) >> (125u - (u >> 23));
    } else {
        u += 0xC2000000u;
    }
    return ((u + 0x7FFFu + ((u >> 16) & 1u)) >> 16) & 0x3FFu;
}

void main() {
    vec4 v = subpassLoad(g_Source) * g_Push.Scale;
    // diagnostics (--gtav_native_fetch_debug): 2 magenta, 3 source + red, 4 source with alpha 1
    if (g_Push.Mode == 2u) v = vec4(1.0, 0.0, 1.0, 1.0);
    if (g_Push.Mode == 3u) v.r += 0.5;
    if (g_Push.Mode == 4u) v.a = 1.0;
    if (g_Push.Mode == 1u)
        v = vec4(Float32To7e3(v.r) / 1023.0, Float32To7e3(v.g) / 1023.0, Float32To7e3(v.b) / 1023.0, clamp(v.a, 0.0, 1.0));
    imageStore(g_Dest[g_Push.DestIndex], ivec2(gl_FragCoord.xy) + g_Push.DestMinusSource, v);
}
