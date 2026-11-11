// gtav - iPhone setup screen: installs the game data on the phone from the player's own Disc 1,
// Disc 2 and Title Update 27 (the game code and shaders inside the app were built on their Mac).
#pragma once

#include <filesystem>
#include <functional>

namespace gtav::ios_setup {

// Documents/data: game/, update/ and user/ (the Mac install's layout).
std::filesystem::path DataRoot();
bool DataInstalled();
// Shows the setup screen over the game window (UI thread). `done` runs on the UI thread once the
// data is installed; the screen stays up (with the error) when an install fails.
void Show(std::function<void()> done);

}  // namespace gtav::ios_setup
