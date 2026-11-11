// On-screen gamepad for touch screens (Android): a synthetic input device the SDK merges into
// guest user 0, and its overlay, drawn with the native renderer's ImGui pass.
#pragma once

#include <memory>

struct ImDrawList;

namespace rex::input {
class InputDriver;
}

namespace gtav::native::touch {

// The touch device; added to the SDK's input system in GtavApp::OnPreSetup.
std::unique_ptr<rex::input::InputDriver> CreateDriver();

// Whether the overlay is shown (cvar gtav_touch_controls, hidden while a real pad is connected).
bool Visible();

// Draws the controls into the overlay (swap chain size w x h pixels).
void Draw(ImDrawList* list, float w, float h);

}  // namespace gtav::native::touch
