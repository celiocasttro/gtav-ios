// gtav - native renderer: write a resolved texture out in the guest's memory layout (Xenos 2D
// tiling, the destination fetch constant's endian swap), for guest data the game reads back on
// the GPU in another form: GTA V's tiled lighting fetches its per-tile data (a resolve) as a
// vertex buffer. The texture heap is set 0 like the other native shaders.

struct MemoryPush {
    uint TextureIndex;
    uint Width;
    uint Height;
    uint PitchBlocks;    // row pitch in texels, aligned to 32 below
    uint64_t Address;    // destination buffer device address (written as two 32-bit words, low first)
    uint BppLog2;        // 2 or 3
    uint Mode;           // packing (bits 0-7) | endian << 8 | linear (not tiled) << 16
    float Scale;         // texels of the texture per guest texel (render scale below 1)
};

[[vk::push_constant]] ConstantBuffer<MemoryPush> g_Push;
Texture2D<float4> g_Textures[] : register(t0, space0);

// Xenos 2D tiled offset in bytes (texture util GetTiledOffset2D).
uint TiledOffset2D(int x, int y, uint pitch, uint bppLog2) {
    pitch = (pitch + 31) & ~31u;
    int macro = ((x >> 5) + (y >> 5) * int(pitch >> 5)) << (bppLog2 + 7);
    int micro = ((x & 7) + ((y & 0xE) << 2)) << bppLog2;
    int offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4);
    return uint(((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
                (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F));
}

uint Swap(uint w, uint endian) {
    if (endian == 1 || endian == 2)
        w = ((w & 0x00FF00FF) << 8) | ((w >> 8) & 0x00FF00FF);  // 8in16
    if (endian == 2 || endian == 3)
        w = (w << 16) | (w >> 16);  // 8in32 = 8in16 then 16in32
    return w;
}

[numthreads(8, 8, 1)]
void CSResolveToMemory(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_Push.Width || id.y >= g_Push.Height)
        return;
    int2 texel = g_Push.Scale == 1.0 ? int2(id.xy) : int2((float2(id.xy) + 0.5) * g_Push.Scale);
    float4 v = g_Textures[g_Push.TextureIndex].Load(int3(texel, 0));
    uint64_t address = g_Push.Address;  // no 64-bit shift: Adreno's compiler asserts on those
    bool untiled = ((g_Push.Mode >> 16) & 1) != 0;
    address += untiled ? ((id.y * ((g_Push.PitchBlocks + 31) & ~31u) + id.x) << g_Push.BppLog2)
                      : TiledOffset2D(int(id.x), int(id.y), g_Push.PitchBlocks, g_Push.BppLog2);
    uint mode = g_Push.Mode & 0xFF, endian = (g_Push.Mode >> 8) & 3;
    if (mode == 0) {  // 16_16_16_16_FLOAT
        uint lo = f32tof16(v.x) | (f32tof16(v.y) << 16), hi = f32tof16(v.z) | (f32tof16(v.w) << 16);
        vk::RawBufferStore<uint>(address, Swap(lo, endian));
        vk::RawBufferStore<uint>(address + 4, Swap(hi, endian));
    } else if (mode == 1) {  // 8_8_8_8
        uint4 b = uint4(round(saturate(v) * 255.0));
        vk::RawBufferStore<uint>(address, Swap(b.x | (b.y << 8) | (b.z << 16) | (b.w << 24), endian));
    } else if (mode == 2) {  // 32_FLOAT
        vk::RawBufferStore<uint>(address, Swap(asuint(v.x), endian));
    } else {  // 16_16_FLOAT
        vk::RawBufferStore<uint>(address, Swap(f32tof16(v.x) | (f32tof16(v.y) << 16), endian));
    }
}
