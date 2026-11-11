// gtav - which guest physical pages were written since a cached copy was checked.
//
// The texture and vertex/index caches used to hash their guest bytes every frame (or every few
// frames, sampled) to notice rewrites: ~5 ms a frame. Linux tracks writes for us: the guest's
// physical views are registered with userfaultfd in async write-protect mode, and once a frame
// Harvest() asks the kernel which pages were written since the last harvest (PAGEMAP_SCAN, which
// write-protects them again in the same call). No signals, no mprotect, no SDK locks: the first
// write to a protected page costs the writing thread a ~0.5 us minor fault. Only pages a copy was
// made from are kept protected (all of memory: ~4,000 faults a frame, ~1 ms of the game's update
// thread). (Re-protecting a copy's whole range at each recheck instead missed writes to pages
// without page tables and kept invalidating neighbouring copies.) (Arming pages
// through the SDK's mprotect watch instead cost ~0.5 ms per call under its global lock and
// stalled the game for up to 250 ms.)
//
// Every page keeps the epoch of the harvest that saw it written. A cached copy stores Watch()
// when it reads (hashes) its guest bytes, and is unchanged while Clean() holds. Anything unsure
// counts as written (pages a view never touched, failures), so mistakes only cost hashing. Writes
// through the raw physical view (the renderer's own writebacks) are reported with Touch().

#pragma once

#include <cstdint>

namespace gtav::native::write_watch {

// False when watching is off (--gtav_native_write_watch=false) or unsupported.
bool Enabled();

// Render thread, once a frame before any lookup: collects the pages written since the last call.
void Harvest();

// Before a copy is made from [physical, physical + size): makes sure its pages are watched and
// returns the epoch to store with the copy (0: not watched, never clean).
uint64_t Watch(uint32_t physical, uint32_t size);

// Diagnostic (verify): written since the last harvest, so the next harvest sees it.
bool Pending(uint32_t physical, uint32_t size);

// True if no page of [physical, physical + size) was written after `epoch`.
bool Clean(uint32_t physical, uint32_t size, uint64_t epoch);

// Marks a range the host wrote through the physical view (not watched).
void Touch(uint32_t physical, uint32_t size);

// Since the last call: harvests, pages seen written, ms spent harvesting.
struct Stats {
  uint64_t harvests, pages;
  double ms;
};
Stats TakeStats();

}  // namespace gtav::native::write_watch
