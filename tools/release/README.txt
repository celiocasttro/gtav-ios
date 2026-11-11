GTA V Xbox 360 recompilation - Windows
=======================================

This package contains no game. It holds a launcher, an Xbox 360 runtime and the tools that turn
your own copy of Grand Theft Auto V for Xbox 360 into a native Windows build on your computer.
No Rockstar Games or Microsoft game code, data, shaders or title-update files are included.

Do not share what you build
---------------------------
Everything this package makes from your discs is Rockstar Games' copyrighted work: the game
folder (data/), the recompiled game library (gtav_game.*), the generated C++, the translated
shaders (data/spirv, shader_cache), and the GTAV.ipa and Android pack made by "Build for iPhone"
and "Build for Android". They are for your own devices only. Never upload, share or link them,
and never add them to a fork or a release. The GPL license of this project covers its own code
only; it gives no one the right to share Rockstar's. Rules for forks and downstream releases:
https://github.com/andrewnakas/gtav-recomp#keeping-it-clean-rules-for-forks-and-downstream-releases

What you need
-------------
- Your own Xbox 360 discs of GTA V: Disc 1 (Install) and Disc 2 (Play), as ISO images or as
  extracted folders.
- Title Update 27 for GTA V from your own console: the TU file (tu00000009_00000000) or a folder
  with disc002\default.xexp and disc002\update.rpf.
- Windows 10/11 x64, a CPU with AVX2 (Intel Haswell / AMD Zen or newer), a GPU with current Vulkan drivers.
- An Xbox-compatible (XInput) controller.
- About 25 GB free on this drive during setup (about 17 GB once installed), 8 GB RAM or more.

First run
---------
1. Unzip this folder somewhere with enough space and run gtav_launcher.exe.
2. Pick Disc 1, Disc 2 and the title update, then press Install.
3. The launcher checks every file against known-good hashes, copies them into data\, generates
   C++ from your game executable, compiles it into gtav_game.dll and removes the build files.
   This takes 15-30 minutes depending on your CPU.
4. The game starts. The first minutes of play translate shaders in the background; some objects
   can be missing until their shader is ready. They are kept in data\spirv for later runs.

Later runs start the game directly. Hold Shift while starting the launcher (or pass --setup) to
open setup again, for example after moving your discs.

iPhone (experimental)
---------------------
Play GTA V on an iPhone 12 or newer (iOS 17+). The phone cannot build the game itself, so this
PC does it once (about 15 minutes); the phone then installs the game files from your discs.
1. Set up and play the game here for a while first (the phone gets the shaders the PC has seen:
   drive around, go indoors).
2. Hold Shift while starting gtav_launcher.exe and press "Build for iPhone". When it is done,
   Explorer shows iphone\GTAV.ipa.
3. Drag GTAV.ipa into Sideloadly (sideloadly.io) with the iPhone plugged in, and sign in with
   your Apple ID. (A free Apple ID has to re-sign every 7 days.)
4. Copy your Disc 1 ISO, Disc 2 ISO and the TU file onto the phone: Apple Devices (or iTunes) >
   your iPhone > Files > GTA V, or put them on a USB stick and plug that into the phone.
5. Open GTA V on the phone and press Install. It copies and checks about 16 GB, then starts.
Objects missing on the phone? Copy the phone's GTA V > shader_cache folder to iphone\shader_cache
here, build for iPhone again (a few minutes) and reinstall GTAV.ipa; the game files stay.

Android (experimental)
----------------------
Play GTA V on an Android phone with a recent Snapdragon (tested: Galaxy S23 FE, Snapdragon 8 Gen 1)
and about 40 GB free during setup. The app has no game in it: this PC builds your game code for
the phone once (about 15-30 minutes), and the phone installs the game files from your discs.
1. Set up and play the game here for a while first (the phone gets the shaders the PC has seen).
2. Hold Shift while starting gtav_launcher.exe and press "Build for Android". When it is done,
   Explorer shows android\gtav-android-pack.zip (android\GTAV.apk is next to it).
3. Copy GTAV.apk, gtav-android-pack.zip, your Disc 1 ISO, Disc 2 ISO and the TU file to the
   phone (for example its Download folder).
4. On the phone, open GTAV.apk to install it (allow installing apps from that source).
5. Open GTA V, choose the four files and press Install. It copies and checks about 16 GB, then
   starts the game. The disc images and the pack can be deleted afterwards.
Objects missing on the phone? Copy the phone's Android\data\com.gtavrecomp.app\files\shader_cache
folder to android\shader_cache here, press "Build for Android" again (a few minutes) and choose
the new pack on the phone; the game files stay.

Everything made from your files stays in this folder (data\ and gtav_game.dll). Do not share it:
it contains Rockstar Games' copyrighted work.

This project is free software under the GNU GPL, version 3 or later (LICENSES\gtav-recomp-GPL-3.0.txt).
Source code: https://github.com/andrewnakas/gtav-recomp. The complete source of every program in
this package that is under the GPL or LGPL (this project, the ReXGlue SDK runtime with the FFmpeg
it links statically, XenosRecomp, plume) is in gtav-recomp-src-<version>.tar.xz on the same
release page.
Licenses for the bundled open-source components are in LICENSES\.
Grand Theft Auto V is a trademark of Take-Two Interactive Software. This project is not affiliated
with or endorsed by Rockstar Games, Take-Two Interactive or Microsoft.
