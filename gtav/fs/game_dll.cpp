// gtav - entry point of the game DLL (Linux: gtav_game.so) built on the player's machine (see gtav_pch.h).
#include "gtav_pch.h"

#ifdef _WIN32
#define GTAVFS_EXPORT __declspec(dllexport)
#else
#define GTAVFS_EXPORT __attribute__((visibility("default")))
#endif

extern "C" GTAVFS_EXPORT const rex::PPCImageInfo* gtav_game_image_config() {
  return &PPCImageConfig;
}
