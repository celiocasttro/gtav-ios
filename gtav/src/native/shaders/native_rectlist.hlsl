// gtav - native renderer: Xenos rect lists. Each rectangle arrives as three corners; the fourth
// is the one opposite the corner with the right angle (the vertex facing the longest edge), and
// every interpolator is extrapolated the same way. The vertex layout matches XenosRecomp's
// vertex shader outputs: position, TEXCOORD0-15, COLOR0-1.

struct Vertex {
    float4 position : SV_Position;
    float4 texcoord[16] : TEXCOORD0;
    float4 color[2] : COLOR0;
};

Vertex Fourth(Vertex a, Vertex b, Vertex corner) {
    Vertex r;
    r.position = a.position + b.position - corner.position;
    [unroll] for (uint i = 0; i < 16; i++) r.texcoord[i] = a.texcoord[i] + b.texcoord[i] - corner.texcoord[i];
    [unroll] for (uint j = 0; j < 2; j++) r.color[j] = a.color[j] + b.color[j] - corner.color[j];
    return r;
}

float Distance2(float4 a, float4 b) {
    float2 d = a.xy / a.w - b.xy / b.w;
    return dot(d, d);
}

[maxvertexcount(4)]
void main(triangle Vertex v[3], inout TriangleStream<Vertex> stream) {
    // Rejected rects: GTA's tiled lighting collapses the tiles a light variant skips to
    // (0,0,0,0). On Xenos they cover nothing; emitted here, w = 0 vertices reach the host
    // rasterizer as screen-centre points and the strip becomes a wedge across the screen.
    // Like the SDK's rect-list shader (which drops primitives with NaN positions), cull them.
    [unroll] for (uint k = 0; k < 3; k++)
        if (!(v[k].position.w > 0.0) || any(isnan(v[k].position)) || any(isinf(v[k].position)))
            return;
    float d01 = Distance2(v[0].position, v[1].position);
    float d12 = Distance2(v[1].position, v[2].position);
    float d20 = Distance2(v[2].position, v[0].position);
    uint c = (d12 >= d01 && d12 >= d20) ? 0 : (d20 >= d01 ? 1 : 2);
    Vertex corner = v[c], a = v[(c + 1) % 3], b = v[(c + 2) % 3];
    stream.Append(corner);
    stream.Append(a);
    stream.Append(b);
    stream.Append(Fourth(a, b, corner));
    stream.RestartStrip();
}
