# OpenIndy3D

A native macOS port of the engine of *Indiana Jones and the Infernal Machine* (1999), for Apple Silicon Macs running
macOS 13 or later. It is built from [infernal-machine-decomp](docs/PORT-PLAN.md), the byte-exact decompilation of the
game's executable, with a Metal renderer, SDL3 input, AVAudioEngine sound and SwiftUI menus.

**A retail copy of the game is required.** OpenIndy3D contains no game data: no levels, models, textures, sounds,
music or movies. It plays the copy you own, version 1.2 in English: the Steam or GOG release, the copy DREAMM installs,
or the original CDs with LucasArts' 1.2 update.

## Playing

1. Install your copy of the game (Steam, GOG or DREAMM) or copy it from the CDs (see below).
2. Open OpenIndy3D. It finds a Steam, GOG or DREAMM copy by itself; otherwise it asks for the game's folder, the one
   that holds `Resource`. Hold Option while it starts to choose another folder.
3. The launcher offers Start New Game, Resume Play, Help (the game's manual, when your copy has one) and Options.

From the CDs: copy the installed game folder, then copy `CD1.GOB` from the first CD's `Resource` folder and `CD2.GOB`
from the second's into the folder's `Resource`, beside `JONES3D.GOB`, and apply the 1.2 update.

Nothing is written into the game's folder. Settings, saves and screenshots are in
`~/Library/Application Support/OpenIndy3D/`, logs in `~/Library/Logs/OpenIndy3D/`. If the game crashes, its last
output is in `Crash.log` there, and the next start says so.

Command line: `OpenIndy3D.app/Contents/MacOS/OpenIndy3D --help` lists `--data`, `--level`, `--windowed` and
`--fullscreen`.

## Building

See [docs/BUILDING.md](docs/BUILDING.md). In short: Xcode, CMake and Ninja; `cmake -S . -B build -G Ninja` and
`cmake --build build` make `build/OpenIndy3D.app`; `tools/check.sh` runs every test. `tools/package.sh` signs,
notarizes and packs a disk image (see BUILDING.md).

## Status

[docs/PORT-PLAN.md](docs/PORT-PLAN.md) is the plan, phase by phase, with what is done and what is open;
[docs/PORTING.md](docs/PORTING.md) records every change the port makes to the decomp. Fixes to retail bugs taken from
OpenJones3D are listed in [docs/UPSTREAM-FIXES.md](docs/UPSTREAM-FIXES.md).

## License and source

OpenIndy3D is free software under the GNU Affero General Public License v3 ([LICENSE](LICENSE)). Its complete source,
including every change, is at https://github.com/Jenavieve-Rose/OpenIndy3D; anyone who receives the app may have it
under the same license. [NOTICE](NOTICE) lists the projects it is built on and credits.

"Indiana Jones" and "LucasArts" are trademarks of their respective owners. OpenIndy3D is not affiliated with or
endorsed by Lucasfilm, LucasArts or The Walt Disney Company.
