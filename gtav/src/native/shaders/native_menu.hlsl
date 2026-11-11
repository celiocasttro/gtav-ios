// gtav - native renderer: settings menu (Dear ImGui draw lists). Uses the backend's pipeline
// layout: texture heap in set 0, samplers in set 3.

struct PushConstants {
    uint2 Unused;        // formerly the vertex device address; kept so the layout stays 24 bytes
    uint TextureIndex;
    uint SamplerIndex;
    float2 InvHalfSize;  // 2 / display size
};

[[vk::push_constant]] ConstantBuffer<PushConstants> g_Push;
Texture2D<float4> g_Textures[] : register(t0, space0);
SamplerState g_Samplers[] : register(s0, space3);

// Vertices come in as an ordinary vertex buffer (ImDrawVert: float2 pos, float2 uv, RGBA8): no
// device addresses or 64-bit integers, which Adreno's compiler mishandles (it crashed on them).
void VSMenu([[vk::location(0)]] float2 pos : POSITION, [[vk::location(1)]] float2 uv_in : TEXCOORD0,
            [[vk::location(2)]] float4 color_in : COLOR0, out float4 position : SV_Position,
            out float2 uv : TEXCOORD0, out float4 color : COLOR0) {
    uv = uv_in;
    color = color_in;
    position = float4(pos * g_Push.InvHalfSize - 1.0, 0.0, 1.0);  // Vulkan: y = -1 is the top
}

float4 PSMenu(float4 position : SV_Position, float2 uv : TEXCOORD0, float4 color : COLOR0) : SV_Target {
    return color * g_Textures[g_Push.TextureIndex].SampleLevel(g_Samplers[g_Push.SamplerIndex], uv, 0);
}
