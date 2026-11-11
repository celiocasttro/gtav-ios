// gtav - workaround: cutscene audio streams never report "finished".
//
// At cutscene end the game calls sub_823F30F0(audio, immediate=0, fade). With audio+5072 set,
// that defers the stream release until each stream stops playing on its own, but in our
// runtime a stream that has played to its end (pos +124 >= end +136) stays "playing" forever,
// so the slot (controller 0x83A320C0 + 10296, 2 slots) is never freed, the audio sits in state 5
// and the next cutscene hangs on load (the state-2 attach handler sub_824DC130 waits on IsBusy).
// While the next cutscene waits, if the audio is in state 5 and its streams are at their end,
// do the immediate release (free the slots with a short fade, then Reset) that the game was
// waiting for. Root cause (missing end-of-stream completion in the runtime) is still open.

#include <rex/hook.h>
#include <rex/logging.h>

#include <cstdint>

namespace {

constexpr uint32_t kAudioObj = 0x83A320F8;   // cutscene audio entity (controller + 56)
constexpr uint32_t kStreamSlots = 0x83A320C0 + 10296;

uint32_t be32(const uint8_t* base, uint32_t addr) {
  const uint8_t* p = base + addr;
  return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

bool StreamAtEnd(const uint8_t* base, int32_t slot_index) {
  if (slot_index < 0) return true;
  uint32_t s = be32(base, kStreamSlots + 4 * uint32_t(slot_index));
  if (s < 0xC0000000 || s >= 0xE0000000) return true;  // free / not a stream
  uint32_t pos = be32(base, s + 124), end = be32(base, s + 136);
  return end != 0 && pos >= end;
}

}  // namespace

REX_EXTERN(__imp__sub_823F30F0);

static void UnstickCutsceneAudio(PPCContext& ctx, uint8_t* base) {
  if (be32(base, kAudioObj + 5068) != 5 || !base[kAudioObj + 5072]) return;
  int32_t a = int32_t(be32(base, kAudioObj + 5044)), b = int32_t(be32(base, kAudioObj + 5048));
  if (!StreamAtEnd(base, a) || !StreamAtEnd(base, b)) return;
  REXLOG_WARN("[cuts] stream(s) {} {} finished but never released: forcing audio release", a, b);
  PPCContext saved = ctx;
  ctx.r3.u64 = kAudioObj;
  ctx.r4.u64 = 1;   // immediate
  ctx.r5.u64 = 10;  // fade ms
  __imp__sub_823F30F0(ctx, base);
  ctx = saved;
}

// CutSceneManager state 2 handler: attach the streamed cut file.
REX_EXTERN(__imp__sub_824DC130);
REX_HOOK_RAW(sub_824DC130) {
  UnstickCutsceneAudio(ctx, base);
  __imp__sub_824DC130(ctx, base);
}
