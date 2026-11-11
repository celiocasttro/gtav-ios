// gtav - native renderer: colour clear as a draw (see Backend DrawClear). vkCmdClearAttachments at
// the start of a render pass made MoltenVK restart the Metal render pass to clear with load
// actions: an extra encoder per clear (~30 a frame). The pipeline's write masks pick the target.
#version 450
layout(push_constant) uniform Push {
    vec4 Color;
} g_Push;
layout(location = 0) out vec4 o0;
layout(location = 1) out vec4 o1;
layout(location = 2) out vec4 o2;
layout(location = 3) out vec4 o3;
void main() {
    o0 = g_Push.Color;
    o1 = g_Push.Color;
    o2 = g_Push.Color;
    o3 = g_Push.Color;
}
