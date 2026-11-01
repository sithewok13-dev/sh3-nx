# Silent Hill 3 on iPhone and iPad

The same port as the Switch build: the PC game's `sh3.exe` recompiled to C, running natively. No emulator, so
no JIT is needed. It draws with OpenGL ES 3.0 at the screen's full resolution.

**Status: experimental.** The renderer passes `tests/run-render-test.sh`, and every source compiles for iOS
(`.github/workflows/ios.yml`), but it has not yet been played through on a phone.

## What you need

- Your own PC copy of Silent Hill 3. The port is built for the `sh3.exe` of 3,338,240 bytes, SHA-1
  `aa2aca6dba19bf510c9c2e92eefdc15e98266581` (`tools/ios/build.sh` checks it).
- A Mac with Xcode, CMake, Ninja and Python 3 (`brew install cmake ninja python`).
- An iPhone or iPad on iOS 15 or later, and a way to install apps yourself: SideStore, AltStore or Sideloadly
  (a free Apple ID works).

## Build

```sh
git clone -b ios https://github.com/sithewok13-dev/sh3-nx
cd sh3-nx
ln -s "/path/to/your/Silent Hill 3" game      # the folder with sh3.exe
tools/ios/build.sh
```

The first run turns `sh3.exe` into C (a few minutes) and builds SDL2 for iOS; later runs only
rebuild what changed. The result is `build/ios/SH3.ipa`.

Generated game code and the `.ipa` contain code derived from the game: keep them to yourself.

## Install

1. Install `build/ios/SH3.ipa` with SideStore, AltStore or Sideloadly.
2. Open the Files app: **On My iPhone > Silent Hill 3** appears once the app has been installed (start it once
   if it does not).
3. Copy these from your PC install there, so they sit at the top (AirDrop to Files, or the Finder's Files tab
   with the phone plugged in, both work):

   ```text
   sh3.exe          the game's exe (the one checked above)
   data/            the game's data, about 5 GB
   savedata/        an empty folder you make: saves and settings go here
   sh3.ini          optional: settings (the launcher's Options write it)
   ``` AirDrop to Files, or the Finder's Files tab with the phone
   plugged in, both work.
4. Start the app. Without the game files it says what is missing.

## Playing

- **Touch controls**: a stick that appears wherever your left thumb lands, A/B/X/Y on the right (Xbox
  colors), dragging elsewhere on the right half for the right stick, L1/L2 and R1/R2 in the top corners, Back
  and Start at the top.
- **Controllers**: Xbox, PlayStation and MFi controllers; pair them in Settings > Bluetooth. While one is
  connected the touch controls hide, and they come back when it disconnects. There are no touch controls yet.
- **Resolution**: *Full screen* (the default) matches the phone's shape, up to 1920 pixels wide, and is scaled
  to the panel's full resolution. The launcher's Options also offer 1080p, 720p and 4:3.
- **Logs**: `sh3.log`, and `sh3-crash.log` after a crash, are written next to the game files, so the Files app
  shows them.

## Notes

- The game reserves 4 GB of address space, as on PC. iOS may refuse that much to apps installed with a free
  Apple ID; the port then runs with a smaller guest heap and says so in `sh3.log` (`guest arena ... MB`). If it
  stops with "guest heap exhausted", installing with a paid developer account and the
  `com.apple.developer.kernel.extended-virtual-addressing` entitlement (`ios/SH3.entitlements`) lifts the limit.
- The app stops drawing while it is in the background, as iOS requires, and carries on where it was.
