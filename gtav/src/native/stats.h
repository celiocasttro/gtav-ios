// gtav - per-second frame time and D3D call-count log (--gtav_native_stats).

#pragma once

#include <chrono>
#include <cstdint>

namespace gtav::native {

enum StatCounter { kStatDrawIndexed, kStatDraw, kStatBeginVerts, kStatDrawOther, kStatVs, kStatPs,
                   kStatAlu, kStatRs, kStatIndices, kStatStreams, kStatCount };

bool StatsEnabled();
void StatCount(StatCounter c);

// Called by the Swap hook around the guest's D3D Swap.
void StatsSwap(std::chrono::steady_clock::time_point begin, std::chrono::steady_clock::time_point end,
               uint32_t device);

}  // namespace gtav::native
