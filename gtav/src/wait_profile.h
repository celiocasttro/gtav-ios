// gtav - where the game's threads wait (see wait_profile.cpp).

#pragma once

#include <rex/ppc.h>

#include <cstdint>

namespace gtav {

// Runs `call` (a hooked game wait function), counted under `wrapper` with --gtav_wait_profile.
void WaitProfileTimed(uint32_t wrapper, PPCContext& ctx, uint8_t* base, void (*call)(PPCContext&, uint8_t*));

}  // namespace gtav
