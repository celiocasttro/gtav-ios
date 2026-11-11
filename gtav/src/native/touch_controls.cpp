// On-screen gamepad (see touch_controls.h). Layout in units of the screen height h, for a
// landscape phone whose game image is letterboxed to 16:9 (the side bars hold the shoulder
// buttons):
//  - left half: a floating left stick (centre where the finger lands);
//  - right half: a floating right stick (camera), unless the touch starts on a button;
//  - face buttons A/B/X/Y bottom right, LB/LT and RB/RT on the left and right edges, L3/R3 below
//    them, the D-pad upper left, Back/Start top centre.
#include "touch_controls.h"

#include "menu.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <rex/cvar.h>
#include <rex/input/input_driver.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
REXCVAR_DEFINE_BOOL(gtav_touch_controls, true, "GTAV", "Show the on-screen gamepad (touch screens)");
#else
REXCVAR_DEFINE_BOOL(gtav_touch_controls, false, "GTAV", "Show the on-screen gamepad (touch screens)");
#endif
// iPhone: rounded corners and the notch eat the edges of the screen, and the controls were too
// small to hit without looking: bigger, and moved in from the sides.
#if defined(__APPLE__) && TARGET_OS_IPHONE
#define GTAV_TOUCH_SCALE_DEFAULT 1.3
#define GTAV_TOUCH_INSET_DEFAULT 0.16
#else
#define GTAV_TOUCH_SCALE_DEFAULT 1.0
#define GTAV_TOUCH_INSET_DEFAULT 0.0
#endif
REXCVAR_DEFINE_DOUBLE(gtav_touch_scale, GTAV_TOUCH_SCALE_DEFAULT, "GTAV",
                      "On-screen gamepad: size of the controls and their distance from the screen sides");
REXCVAR_DEFINE_DOUBLE(gtav_touch_inset, GTAV_TOUCH_INSET_DEFAULT, "GTAV",
                      "On-screen gamepad: extra distance of the controls from the left / right edges (screen heights)");

namespace gtav::native::touch {
namespace {

using namespace rex::input;
using rex::X_RESULT;
using rex::X_STATUS;

enum Kind { kButton, kTrigger, kMenu };

struct Control {
  const char* label;
  Kind kind;
  uint16_t button;   // X_INPUT_GAMEPAD_* for kButton
  bool right;        // kTrigger: right trigger
  float x, y, r;     // centre and radius; x < 0 counts from the right edge, in units of h
};

// Positions in units of the screen height; negative x is measured from the right edge.
const Control kControls[] = {
    {"A", kButton, X_INPUT_GAMEPAD_A, false, -0.36f, 0.86f, 0.075f},
    {"B", kButton, X_INPUT_GAMEPAD_B, false, -0.22f, 0.72f, 0.075f},
    {"X", kButton, X_INPUT_GAMEPAD_X, false, -0.50f, 0.72f, 0.075f},
    {"Y", kButton, X_INPUT_GAMEPAD_Y, false, -0.36f, 0.58f, 0.075f},
    {"RB", kButton, X_INPUT_GAMEPAD_RIGHT_SHOULDER, false, -0.09f, 0.14f, 0.07f},
    {"RT", kTrigger, 0, true, -0.09f, 0.34f, 0.08f},
    {"R3", kButton, X_INPUT_GAMEPAD_RIGHT_THUMB, false, -0.09f, 0.53f, 0.055f},
    {"LB", kButton, X_INPUT_GAMEPAD_LEFT_SHOULDER, false, 0.09f, 0.14f, 0.07f},
    {"LT", kTrigger, 0, false, 0.09f, 0.34f, 0.08f},
    {"L3", kButton, X_INPUT_GAMEPAD_LEFT_THUMB, false, 0.09f, 0.53f, 0.055f},
    {"^", kButton, X_INPUT_GAMEPAD_DPAD_UP, false, 0.36f, 0.12f, 0.05f},
    {"v", kButton, X_INPUT_GAMEPAD_DPAD_DOWN, false, 0.36f, 0.32f, 0.05f},
    {"<", kButton, X_INPUT_GAMEPAD_DPAD_LEFT, false, 0.26f, 0.22f, 0.05f},
    {">", kButton, X_INPUT_GAMEPAD_DPAD_RIGHT, false, 0.46f, 0.22f, 0.05f},
    {"Back", kButton, X_INPUT_GAMEPAD_BACK, false, 0.0f, 0.06f, 0.05f},   // x set at runtime (centre)
    {"Start", kButton, X_INPUT_GAMEPAD_START, false, 0.0f, 0.06f, 0.05f},
    {"Menu", kMenu, 0, false, 0.0f, 0.06f, 0.05f},  // the settings menu (centre top)
};
constexpr int kControlCount = int(sizeof(kControls) / sizeof(kControls[0]));
constexpr float kStickRadius = 0.14f;  // full deflection, units of h

enum Owner { kNone = -1, kLeftStick = -2, kRightStick = -3 };

struct Finger {
  int owner = kNone;  // control index, or a stick
  float x0 = 0, y0 = 0, x = 0, y = 0;  // pixels
};

std::mutex g_mutex;
std::unordered_map<SDL_FingerID, Finger> g_fingers;
std::atomic<float> g_w{0}, g_h{0};
std::atomic<uint32_t> g_packet{0};
std::atomic<bool> g_watch_added{false};

float Scale() { return float(std::clamp(REXCVAR_GET(gtav_touch_scale), 0.5, 2.0)); }
float Inset() { return float(std::clamp(REXCVAR_GET(gtav_touch_inset), 0.0, 0.5)); }

float CenterX(int i, float w, float h) {
  const Control& c = kControls[i];
  const float s = Scale();
  if (c.kind == kMenu) return w * 0.5f;
  if (c.button == X_INPUT_GAMEPAD_BACK) return w * 0.5f - 0.13f * s * h;
  if (c.button == X_INPUT_GAMEPAD_START) return w * 0.5f + 0.13f * s * h;
  const float from_edge = (std::fabs(c.x) * s + Inset()) * h;
  return c.x < 0 ? w - from_edge : from_edge;
}

// Rows keep their order from top to bottom; scaled up, they are squeezed back onto the screen.
float CenterY(int i, float h) {
  const Control& c = kControls[i];
  const float r = c.r * Scale();
  return std::clamp(c.y, r + 0.02f, 1.0f - r - 0.02f) * h;
}

float Radius(int i, float h) { return kControls[i].r * Scale() * h; }

int Hit(float x, float y, float w, float h) {
  for (int i = 0; i < kControlCount; i++) {
    float dx = x - CenterX(i, w, h), dy = y - CenterY(i, h);
    float r = Radius(i, h) * 1.25f;  // fingers are wide: a little slack
    if (dx * dx + dy * dy <= r * r) return i;
  }
  return kNone;
}

bool OnEvent(void*, SDL_Event* e) {
  if (e->type != SDL_EVENT_FINGER_DOWN && e->type != SDL_EVENT_FINGER_MOTION && e->type != SDL_EVENT_FINGER_UP &&
      e->type != SDL_EVENT_FINGER_CANCELED)
    return true;
  float w = g_w.load(), h = g_h.load();
  if (w <= 0 || h <= 0) return true;
  float x = e->tfinger.x * w, y = e->tfinger.y * h;
  std::lock_guard lock(g_mutex);
  if (e->type == SDL_EVENT_FINGER_DOWN) {
    Finger f;
    f.owner = Hit(x, y, w, h);
    if (f.owner >= 0 && kControls[f.owner].kind == kMenu) {
      menu::OnEscape();  // toggles the settings menu; the finger is not a game input
      return true;
    }
    // While the menu is open touches drive it (SDL turns them into mouse events), not the game.
    if (menu::IsOpen()) return true;
    if (f.owner == kNone) f.owner = x < w * 0.5f ? kLeftStick : kRightStick;
    f.x0 = f.x = x;
    f.y0 = f.y = y;
    g_fingers[e->tfinger.fingerID] = f;
  } else if (e->type == SDL_EVENT_FINGER_MOTION) {
    auto it = g_fingers.find(e->tfinger.fingerID);
    if (it != g_fingers.end()) {
      it->second.x = x;
      it->second.y = y;
    }
  } else {
    g_fingers.erase(e->tfinger.fingerID);
  }
  g_packet++;
  return true;
}

int16_t Axis(float v) {
  v = std::clamp(v, -1.0f, 1.0f);
  return int16_t(std::lround(v * 32767.0f));
}

void Fill(X_INPUT_GAMEPAD* pad) {
  float h = g_h.load();
  if (menu::IsOpen()) {
    std::lock_guard lock(g_mutex);
    g_fingers.clear();
  }
  uint16_t buttons = 0;
  uint8_t lt = 0, rt = 0;
  float lx = 0, ly = 0, rx = 0, ry = 0;
  std::lock_guard lock(g_mutex);
  for (auto& [id, f] : g_fingers) {
    if (f.owner >= 0) {
      const Control& c = kControls[f.owner];
      if (c.kind == kTrigger)
        (c.right ? rt : lt) = 0xFF;
      else
        buttons |= c.button;
    } else if (h > 0) {
      float dx = (f.x - f.x0) / (kStickRadius * Scale() * h), dy = -(f.y - f.y0) / (kStickRadius * Scale() * h);
      float len = std::sqrt(dx * dx + dy * dy);
      if (len > 1.0f) dx /= len, dy /= len;
      if (f.owner == kLeftStick) lx = dx, ly = dy;
      else rx = dx, ry = dy;
    }
  }
  pad->buttons = buttons;
  pad->left_trigger = lt;
  pad->right_trigger = rt;
  pad->thumb_lx = Axis(lx);
  pad->thumb_ly = Axis(ly);
  pad->thumb_rx = Axis(rx);
  pad->thumb_ry = Axis(ry);
}

bool RealPadConnected() {
  int count = 0;
  SDL_JoystickID* pads = SDL_GetGamepads(&count);
  SDL_free(pads);
  return count > 0;
}

constexpr DeviceId kTouchDevice = static_cast<DeviceId>(0x544F5543);  // "TOUC"

class TouchDriver final : public InputDriver {
 public:
  TouchDriver() : InputDriver(nullptr, 0) {}
  X_STATUS Setup() override { return X_STATUS_SUCCESS; }
  void EnumerateDevices(std::vector<DeviceInfo>& out) override {
    if (!REXCVAR_GET(gtav_touch_controls)) return;
    DeviceInfo info;
    info.id = kTouchDevice;
    info.name = "Touch screen";
    info.synthetic = true;
    out.push_back(info);
  }
  X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* out) override {
    if (id != kTouchDevice || !REXCVAR_GET(gtav_touch_controls)) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (!g_watch_added.exchange(true)) SDL_AddEventWatch(OnEvent, nullptr);
    if (out) {
      std::memset(out, 0, sizeof(*out));
      out->packet_number = g_packet.load();
      Fill(&out->gamepad);
    }
    return X_ERROR_SUCCESS;
  }
  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t, X_INPUT_CAPABILITIES* caps) override {
    if (id != kTouchDevice || !REXCVAR_GET(gtav_touch_controls)) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (caps) {
      std::memset(caps, 0, sizeof(*caps));
      caps->type = 0x01;
      caps->sub_type = 0x01;
      caps->gamepad.buttons = 0xFFFF;
      caps->gamepad.left_trigger = 0xFF;
      caps->gamepad.right_trigger = 0xFF;
      caps->gamepad.thumb_lx = int16_t(0x7FFF);
      caps->gamepad.thumb_ly = int16_t(0x7FFF);
      caps->gamepad.thumb_rx = int16_t(0x7FFF);
      caps->gamepad.thumb_ry = int16_t(0x7FFF);
    }
    return X_ERROR_SUCCESS;
  }
  X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION*) override {
    return id == kTouchDevice ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
  }
  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t, X_INPUT_KEYSTROKE*) override {
    return id == kTouchDevice ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
  }
};

}  // namespace

std::unique_ptr<rex::input::InputDriver> CreateDriver() { return std::make_unique<TouchDriver>(); }

bool Visible() {
  if (!REXCVAR_GET(gtav_touch_controls)) return false;
  // Re-checked twice a second: a pad connected over Bluetooth hides the overlay.
  static std::atomic<uint64_t> next_check{0};
  static std::atomic<bool> pad{false};
  uint64_t now = SDL_GetTicks();
  if (now >= next_check.load()) {
    next_check = now + 500;
    pad = RealPadConnected();
  }
  return !pad.load();
}

void Draw(ImDrawList* list, float w, float h) {
  g_w = w;
  g_h = h;
  if (!g_watch_added.exchange(true)) SDL_AddEventWatch(OnEvent, nullptr);
  bool held[kControlCount] = {};
  struct Stick { bool on; float x0, y0, x, y; } sticks[2] = {};
  {
    std::lock_guard lock(g_mutex);
    for (auto& [id, f] : g_fingers) {
      if (f.owner >= 0) held[f.owner] = true;
      else sticks[f.owner == kLeftStick ? 0 : 1] = {true, f.x0, f.y0, f.x, f.y};
    }
  }
  const ImU32 fill = IM_COL32(255, 255, 255, 40), fill_on = IM_COL32(255, 255, 255, 110);
  const ImU32 edge = IM_COL32(255, 255, 255, 120), text = IM_COL32(255, 255, 255, 200);
  const bool menu_open = menu::IsOpen();
  for (int i = 0; i < kControlCount; i++) {
    if (menu_open && kControls[i].kind != kMenu) continue;
    ImVec2 c(CenterX(i, w, h), CenterY(i, h));
    float r = Radius(i, h);
    list->AddCircleFilled(c, r, held[i] ? fill_on : fill, 32);
    list->AddCircle(c, r, edge, 32, 2.0f);
    const char* label = kControls[i].label;
    float size = r * 0.8f;
    ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, label);
    list->AddText(ImGui::GetFont(), size, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), text, label);
  }
  for (auto& s : sticks) {
    if (!s.on) continue;
    float R = kStickRadius * Scale() * h;
    list->AddCircle(ImVec2(s.x0, s.y0), R, edge, 48, 2.0f);
    float dx = s.x - s.x0, dy = s.y - s.y0, len = std::sqrt(dx * dx + dy * dy);
    if (len > R) dx *= R / len, dy *= R / len;
    list->AddCircleFilled(ImVec2(s.x0 + dx, s.y0 + dy), R * 0.4f, fill_on, 32);
  }
}

}  // namespace gtav::native::touch
