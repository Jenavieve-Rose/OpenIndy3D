# Building the port

How to build J3DPort on an Apple Silicon Mac, point it at your copy of the game, and run the tests.

The decomp's own build instructions (the byte-matching pipeline, which the port does not use) are kept as
[DECOMP-BUILDING.md](DECOMP-BUILDING.md).

## Requirements

- An Apple Silicon Mac. The build targets arm64 and macOS 13.0 or later.
- Xcode or the Command Line Tools, for Apple clang.
- CMake 3.24 or later, and Ninja: `brew install cmake ninja`.
- SDL3 3.2 or later, for the window and input: `brew install sdl3`. Not needed for a `-DJ3D_PLATFORM=null` build.
- Python 3, only for the scripts in `tools/`.
- A retail copy of the game, version 1.2, for running and testing. Nothing from it is needed to compile.

Last checked with macOS 27.2, Xcode 27.2 (Apple clang 21), CMake 4.4 and Ninja.

## Game data

The port never runs from the game folder. It is told where the data is: the **data root** is the folder that holds
`RESOURCE/` (with `CD1.GOB`, `CD2.GOB`, `JONES3D.GOB` and the loose `Cog/` scripts of the 1.2 update). Names are matched
case-insensitively, so `Resource/` or `resource/` work too.

The data root comes from, in order:

1. `--data <dir>` on the `jones3d` command line,
2. the `J3D_DATA` environment variable,
3. `Data Root` in the settings file.

## Build with Ninja

From the repository root:

```sh
cmake -S . -B build -G Ninja -DJ3D_TEST_DATA=/path/to/data-root
cmake --build build
```

`-DJ3D_TEST_DATA` is optional. Without it everything still builds, and only the tests that need no game data
(`display`, `present`, `render`, `keys`) are registered with `ctest`.

| Output | What it is |
| --- | --- |
| `build/jones3d` | The game, with the backends `J3D_PLATFORM` selects. |
| `build/jones3d-null` | The game with the null backends (apple builds only). The `boot` test runs it. |
| `build/levelload`, `build/cogcheck`, `build/saveload`, `build/display`, `build/present`, `build/render`, `build/keys` | The test programs (below). They use the null backends, apart from `present` and `render` (apple builds only). |
| `build/lib*.a` | The engine libraries: `zlib`, `std`, `rdroid`, `sound`, `smush`, `sith`, `jones3d_game`, and the platform: `platform_core`, `platform_null`, `platform_apple`. |

## Build with Xcode

```sh
cmake -S . -B build-xcode -G Xcode -DJ3D_TEST_DATA=/path/to/data-root
open build-xcode/J3DPort.xcodeproj
```

or, without opening Xcode, `cmake --build build-xcode --config Debug`. Binaries go to `build-xcode/Debug/`. The
`jones3d` scheme runs with the repository root as its working directory; add `--data <dir>` to its arguments.

Regenerate the project with the first command after adding or removing source files. Do not edit the generated project:
`CMakeLists.txt` is the source of truth.

## CMake options

| Option | Default | Effect |
| --- | --- | --- |
| `J3D_PLATFORM` | `apple` | `apple`: SDL3 window and input, Metal renderer (needs SDL3). `null`: the null backends only, no window; builds without SDL3. Tests use the null backends either way. |
| `J3D_TEST_DATA` | empty | Data root used by the tests. Registers the tests that need game data with `ctest` when set. |
| `J3D_DEBUG_ASSERTS` | `OFF` | Compiles the engine's debug-only `J3D_ASSERT` checks. Off matches the decomp's release build. |
| `J3D_WERROR_SHORTEN` | `ON` | Makes `-Wshorten-64-to-32` an error. `Jones3D/Display/jonesConfig.c` is exempt until Phase 6 rewrites its dialog code (PORT-PLAN.md P6.24). |
| `J3D_SANITIZE` | `OFF` | Builds everything, tests included, with AddressSanitizer and UndefinedBehaviorSanitizer. Use a separate build directory (below). |
| `CMAKE_BUILD_TYPE` | none | `Debug` or `RelWithDebInfo` as usual, for the Ninja generator. |

Every compiler warning is an error (`-Werror`), apart from two known sets: the 58 narrowing conversions in
`jonesConfig.c` and one unsequenced write in `Libs/sound/AudioLib.c` (PORT-PLAN.md P4.8). A clean build prints exactly
those 59 warnings, and in an apple build one linker warning for each program that links SDL3 (`jones3d`, `present`, `render`): Homebrew's SDL3 is built for the current macOS, newer than
the 13.0 deployment target, so a binary built this way runs only on a macOS as new as the build machine's until SDL3
ships inside the app bundle (PORT-PLAN.md P8.3). The MSVC-dialect warnings the decomp would otherwise raise (missing braces, `sprintf` deprecation
and so on) are switched off in `CMakeLists.txt`.

### Sanitizer build

```sh
cmake -S . -B build-asan -G Ninja -DJ3D_SANITIZE=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DJ3D_TEST_DATA=/path/to/data-root
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

The tests stop at the first UBSan report (`UBSAN_OPTIONS=halt_on_error=1` is set for them). Run a program by hand with
`UBSAN_OPTIONS=halt_on_error=0` to see every report in one run. Leak detection is off: ASan does not support it on
Apple silicon.

## Running

```sh
build/jones3d --data /path/to/data-root
```

| Option | Effect |
| --- | --- |
| `--data <dir>` | The data root. |
| `--headless` | Run with an in-memory display and no window, on a virtual clock (33 ms of game time per frame, not tied to real time). For tests. In an apple build the SDL event loop still runs, on SDL's `dummy` video driver; `SDL_VIDEO_DRIVER=cocoa` shows the (still black) window instead. |
| `--frames <n>` | Quit after n passes of the game loop. Once a level is open a pass is one frame. |
| `--screenshot-at <n>[,<n>...]` | Write the back buffer as a BMP after those passes of the game loop. Each file written is printed. |
| `--screenshot-file <path>` | Where screenshots go; `{frame}` becomes the frame number. Default `jones3d-{frame}.bmp` in the current directory. |
| `--record <file>` | Record the run's input and clock to `<file>` (apple builds; `platform/common/host_record.h`). |
| `--replay <file>` | Play a recording back instead of reading the keyboard, mouse and controllers, then quit. Exits with failure if the run goes out of step with the recording (other data, settings or engine). With `--headless` it runs as fast as the host allows. |
| `--input-script <file>` | Feed scripted input, for tests: lines `<frame> key UP down`, `<frame> mouse 40 0`, `<frame> pad 1 down`, `<frame> axis y -32767`, `<frame> hat left`, `<frame> vkey F5`. The format is at the top of `platform/common/host_record.h`. |

Any other argument is passed to the engine's own command-line parser unchanged.

In an apple build `jones3d` first prints a `Host:` line (macOS version, Metal device, SDL version), opens the game
window and runs the game in it, fullscreen by default, rendered through Metal. Close the window, or press Cmd-Q, to
quit. To run in a window instead, at a size from the mode list:

```sh
J3D_REG_InWindow=1 J3D_REG_Width=800 J3D_REG_Height=600 build/jones3d --data /path/to/data-root
```

A 4:3 mode on a wide screen is pillarboxed (black bars at the sides); `J3D_REG_Pillarbox=0`, or `Pillarbox=i:0` in the
settings file, stretches it instead. Alt+Return or Cmd+Ctrl+F switches between fullscreen and a window.

### Controls

The keyboard, mouse and controllers work as in the retail game (PORT-PLAN.md Phase 3), through the key set named by the
`Configuration` setting: a `.kfg` file in `KeySets/` (the retail ones are copied there on the first run), or one of the
built-in defaults. With nothing configured the game picks the keyboard default, or, when a modern two-stick controller
is connected, the port's "Dual-Stick Controller" set: left stick to move and turn, right stick to sidestep, A jump,
B crawl, X activate, Y draw or holster, LB look, RB run, left stick click roll, right stick click lighter, Back map,
Start inventory, d-pad left and right weapons, down health, up chalk. Every key set keeps the keyboard keys as well.

- The Option keys are Alt and the Command keys are the Windows keys. Keys are read by position, as on a US keyboard.
- In fullscreen, or with `Mouse Control=i:1`, the game takes the mouse (no cursor) while it has focus; Cmd-Tab or any
  other loss of focus gives it back. Mouse movement only turns and looks with `Mouse Control=i:1`, as in retail.
  `Mouse Scale=i:<percent>` (default 100) scales its motion.
- `Joystick Control` is on by default in the port (retail: off until set in the controls dialog). Controllers can be
  connected and disconnected while playing; up to eight. An analog stick walks at part deflection and runs at full.
- The controls dialog, where retail chose and edited key sets, arrives with the other dialogs (PORT-PLAN.md Phase 6);
  until then edit `Configuration` or the `.kfg` files.

A null build has no display: without `--headless` it opens the GOBs, loads its string tables, starts the engine and
exits when it enumerates the displays. With `--headless` either build loads the first level and runs:

```sh
build/jones3d --data /path/to/data-root --headless --frames 600
```

Nothing is written into your game folder. Saves (`SaveGames/`), screenshots (`ScreenShots/`) and keysets
(`KeySets/`) go to `~/Library/Application Support/J3DPort/`; the first run copies the retail keysets there.
`JonesError.txt` and `JonesLog.txt` go to `~/Library/Logs/J3DPort/`. Paths may be up to 1023 characters, both
for these folders and for the data root (PORT-PLAN.md P1.30). PORT-PLAN.md tracks what works.

A headless run is deterministic: two runs with the same data and settings print the same log and write
byte-identical saves.

### Settings and environment variables

Settings are stored in `~/Library/Application Support/J3DPort/settings.ini`, one `<name>=<type>:<value>` per line, using
the value names the retail game kept in the registry. The format is described at the top of
`platform/common/wuRegistry_file.c`.

| Variable | Effect |
| --- | --- |
| `J3D_DATA` | The data root. `--data` sets it. |
| `J3D_SETTINGS` | Path of the settings file to use instead of the default. `/dev/null` runs with defaults and saves nothing. |
| `J3D_REG_<name>` | Overrides one setting for this run; spaces in the name become underscores, for example `J3D_REG_Start_Mode=2`. `J3D_REG_Install_Path=<folder>/` moves saves, screenshots and keysets. |
| `J3D_LOGS` | Folder for `JonesError.txt` and `JonesLog.txt` instead of `~/Library/Logs/J3DPort/`. |
| `J3D_TRACE_FILES` | `1` prints every host file that opens successfully to stderr, which shows the loose files overriding GOB contents. |
| `J3D_HEADLESS` | `1` is `--headless`. |
| `J3D_FRAMES` | `<n>` is `--frames <n>`. |
| `J3D_SCREENSHOT_AT`, `J3D_SCREENSHOT_FILE` | `--screenshot-at` and `--screenshot-file`. |
| `J3D_RECORD`, `J3D_REPLAY`, `J3D_INPUT_SCRIPT` | `--record`, `--replay` and `--input-script`. |

## Tests

The tests are headless programs that start the engine without display, input, sound output or dialogs and run against
your own game data. They never touch your settings file.

```sh
ctest --test-dir build --output-on-failure        # Ninja tree
ctest --test-dir build-xcode -C Debug             # Xcode tree
```

| Test | What it checks | Time |
| --- | --- | --- |
| `levelload` | Loads the static world and all 17 levels through every binary `.cnd` section loader. | about 40 s |
| `cogcheck` | Compiles every `.cog` in the three GOBs through the engine's lookup, so loose overrides are the files compiled. | about 2 s |
| `saveload` | Runs the game headless for 300 frames, saves, restores the save into a freshly loaded level and compares the world (things, positions, sectors, health, COG globals, game time), saves again and compares the two files, then plays 300 more frames. Saves go to `build/saveload-out/SaveGames/`. | under 1 s |
| `boot` | Runs `jones3d --headless --frames 600` (`jones3d-null` in an apple build): level open, the level-start autosave and 20 s of game time in the first level. Must exit 0 with no assert. Its autosave goes to `build/boot/SaveGames/`, not the game folder. | under 1 s |
| `display` | No game data needed. The back buffer, fills, every blit case (8, 16 and 32 bpp, clipped, colour-keyed) against a pixel-by-pixel reference, the RGB565 encoder and a `SaveScreen` BMP. | under 1 s |
| `present` | Apple builds only, no game data needed. Presents a pattern through `stdDisplay` in a real window (it flashes up briefly), reads the screen image back from Metal and compares it, unscaled and scaled; then checks that a frame with a 3D scene presents the scene's picture. Skipped without a window server, for example over SSH. | under 1 s |
| `render` | Apple builds only, no game data or window needed. Draws with the Metal renderer offscreen and reads the result back: pixel coverage, colour order, depth test and clears, alpha test and blending, perspective-correct interpolation, depth clamping, lines and points. | under 1 s |
| `boot-sdl` | Apple builds only: the same run through `jones3d`, so the SDL event loop delivers the frames and the closing `WM_CLOSE` (SDL's `dummy` video driver, no window). Its autosave goes to `build/boot-sdl/SaveGames/`. | under 1 s |
| `keys` | No game data needed. The DIK tables (HID usage to DirectInput key, key names), `GetKeyNameTextA` and `SearchPathA`. | under 1 s |
| `input` | Apple builds only (`tests/input.sh`). Scripted headless runs of level 2 that quicksave at frame 800: holding a key moves Indy; a recorded run replays to a byte-identical quicksave, and recording does not change the run; a scripted controller's A jumps exactly as the jump key does (the port's default key set); with the retail "Microsoft SideWinder game pad" key set its button 7 sidesteps exactly as A does; mouse motion turns Indy. Runs go to `build/input-out/`. | about 10 s |

Each can be run directly, which shows its output:

```sh
build/levelload /path/to/data-root                  # all levels, with section counts per level
build/levelload /path/to/data-root 00_cyn.cnd       # just the levels named
build/cogcheck  /path/to/data-root
J3D_REG_Install_Path=/tmp/j3d/ build/saveload /path/to/data-root 3000 3000   # frames before the save, after the restore
```

`saveload` needs `J3D_REG_Install_Path` set to a folder it may write to. The second save may differ from the first
only in three ways that are retail behaviour, which the comparison allows and PORTING.md describes ("Savegame round
trip"); anything else fails the test.

The exit status is the number of failures. Lines such as `Symbol … not defined. Assuming float=0.0` come from
undeclared symbols in the retail scripts and are not failures. An engine assert or a crash prints a backtrace.

To add a test, put `tests/<name>.c` in place (use `tests/harness.h` for startup) and add `<name>` to `J3D_TESTS` in
`CMakeLists.txt`.

## Tools

Run these from the repository root.

| Command | Purpose |
| --- | --- |
| `python3 tools/layout_audit.py` | Compares the layout of every on-disk record between the retail 32-bit ABI and arm64. Needs a configured `build/` tree. Must end with `0 retail on-disk type(s) differ`. `--all` lists every differing record, not only on-disk ones. |
| `python3 tools/gobtool.py list <file.gob> [glob]` | Lists a GOB's entries, for example `'*.cnd'`. |
| `python3 tools/gobtool.py extract <file.gob> <outdir> [glob]` | Extracts matching entries. Extracted files are game data: keep them out of the repository. |
| `tools/level_shots.sh <data-root> <out-dir> [frames]` | Screenshots of all 17 levels, headless, at the given frames (default `1,300`). Needs an apple build in `build/`. |
| `python3 tools/gen_null_platform.py` | Regenerates `platform/common/null/*_null.c` from the public headers of the replaced modules. Run it after changing the script's overrides or its `IMPLEMENTED_ELSEWHERE` set. |

## Before committing

```sh
cmake --build build && ctest --test-dir build --output-on-failure && python3 tools/layout_audit.py
cmake --build build-asan && ctest --test-dir build-asan --output-on-failure
```

Record any change to the imported sources in [PORTING.md](PORTING.md).
