// gtav - ReXGlue Recompiled Project

#if GTAV_GAME_DLL
// The game code lives in gtav_game.dll (Linux: gtav_game.so, found through the $ORIGIN rpath),
// built by the installer from the player's own discs.
#include <rex/image_info.h>
#ifdef _WIN32
extern "C" __declspec(dllimport) const rex::PPCImageInfo* gtav_game_image_config();
#else
extern "C" const rex::PPCImageInfo* gtav_game_image_config();
#endif
#define PPCImageConfig (*gtav_game_image_config())
#else
#include "generated/default/gtav_init.h"
#endif

#include "gtav_app.h"

REX_DEFINE_APP(gtav, GtavApp::Create)
