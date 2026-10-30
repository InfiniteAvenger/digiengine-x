# DigiEngine X

A data-driven, open-source virtual pet engine inspired by classic 3-button
v-pets. The engine contains **no copyrighted creatures, names or art**: all
content is loaded from external *packs* (sprites + JSON). An original demo pack
ships in `packs/demo/` so the engine is playable out of the box.

Targets: Windows / Linux PC, Android, and ESP32-S3 handheld hardware.

The project started as "vpet", and that short name stays on the inside: the
code namespace, build targets and executables, the save folder and the
Android package ID, so existing saves and installs carry over.

## Layout

```
engine/          Portable C++17 game core (no platform code, no OS calls)
  include/vpet/  Public headers
  src/           Implementation
platform/sdl/    PC and Android frontend using SDL2
platform/shell/  The handheld frame with touch buttons (Android)
platform/android/ Android app (Gradle + SDL2); see its README to build the APK
platform/esp32/  ESP32-S3 frontend (Waveshare ESP32-S3-LCD-1.69)
tools/baker/     Pack baker: CSV tables + sprites -> pack folder
packs/demo/      Original CC0 demo pack
docs/            Design doc and pack format reference
```

## Building (Windows, MSYS2 UCRT64)

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2
cmake -S . -B build -G Ninja
cmake --build build
./build/vpet.exe
```

## Controls (desktop)

| Button | Keys            | Home screen                  |
|--------|-----------------|------------------------------|
| A      | `Z`, `1`, Left  | Cycle menu selection         |
| B      | `X`, `2`, Down  | Confirm selected item        |
| C      | `C`, `3`, Right | Cancel / back                |

`Esc` quits. **Hold A+C for 3 seconds** on any screen to open settings: switch
packs and versions, pick equipped moves, turn sound on or off, set the clock,
and see pack info.

Every folder in `packs/` with a `pack.json` is a pack. Put your own packs there
(they stay out of git) and they are copied next to the game when it builds.
Pass a folder as the first argument to load packs from somewhere else.

Saves live in `%APPDATA%\vpet\vpet\<pack id>.sav` (a readable text file).
Delete it to start over with a new egg.

## Testing

`vpet_snapshot` plays a scripted session headlessly with a fake clock, checks
the care rules, and writes screenshots. It exits non-zero if a check fails.

```bash
./build/vpet_snapshot.exe packs/demo build/shots
```

## Legal

This project is not affiliated with or endorsed by any toy or media company.
Packs containing third-party content must be obtained and used by players
themselves and are excluded from this repository by `.gitignore`.
