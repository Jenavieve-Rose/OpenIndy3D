# Building the port

How to build OpenIndy3D on an Apple Silicon Mac, point it at your copy of the game, and run the tests.

The decomp's own build instructions (the byte-matching pipeline, which the port does not use) are kept as
[DECOMP-BUILDING.md](DECOMP-BUILDING.md).

## Requirements

- An Apple Silicon Mac. The build targets arm64 and macOS 13.0 or later.
- Xcode or the Command Line Tools, for Apple clang.
- CMake 3.24 or later, and Ninja: `brew install cmake ninja`.
- SDL3 is downloaded and built by the first configure of an apple tree: the pinned 3.4.20 release, hash-checked,
  linked statically for macOS 13 (PORT-PLAN.md P8.3). That needs the network once per build tree.
  `-DJ3D_SYSTEM_SDL3=ON` takes an installed SDL3 (`brew install sdl3`) instead, for development only: Homebrew's
  needs the macOS it was built on. Not needed at all for a `-DJ3D_PLATFORM=null` build.
- Nothing for sound: the output device is AVAudioEngine, part of macOS.
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
3. `Data Root` in the settings file,
4. otherwise the game looks in the Steam, GOG and DREAMM install places (also inside CrossOver and Whisky bottles),
   stores what it finds, and in a run someone is at asks with an open panel; holding Option at launch asks again.
   The folder is checked: the three GOBs must be there, and `Resource/Indy3D.exe` names the version (P8.4, P8.5).

When the data root comes from 3 or 4, the game copies what it needs to run (`Resource/` and `KeySets/`) into
`~/Library/Application Support/OpenIndy3D/GameData/`, checks the copy, and from then on runs from it (`Data Root` names
it; `Data Source` records where it came from). The store's folder is left alone. On the same APFS volume the files are
cloned, so the copy is instant and takes no space; elsewhere it is a full copy (about 860 MB). `J3D_NO_DATA_COPY=1`
runs from the folder as it is. A data root given with `--data` or `J3D_DATA` (the tests and tools) is never copied.

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
| `build/OpenIndy3D.app` | The game as an app (apple builds), with its icon, the About text, `LICENSE` and `NOTICE` (P8.2, P8.12). |
| `build/jones3d` | In an apple build, a link to the app's executable; in a null build, the game itself. |
| `build/jones3d-null` | The game with the null backends (apple builds only). The `boot` test runs it. |
| `build/levelload`, `build/cogcheck`, `build/saveload`, `build/display`, `build/present`, `build/render`, `build/keys` | The test programs (below). They use the null backends, apart from `present` and `render` (apple builds only). |
| `build/lib*.a` | The engine libraries: `zlib`, `std`, `rdroid`, `sound`, `smush`, `sith`, `jones3d_game`, and the platform: `platform_core`, `platform_null`, `platform_apple`. |

### Universal (Apple silicon and Intel)

```sh
tools/universal.sh --data /path/to/data-root --test
```

builds an arm64 tree (`build-arm64/`) and an x86_64 tree (`build-x86_64/`), runs both trees' tests with `--test` (the
x86_64 ones under Rosetta 2), and joins the two apps' executables with `lipo` into `build-universal/OpenIndy3D.app`
(phase 9). One tree cannot hold both architectures: CMake's Ninja generator does not build Swift for two at once.
Options after `--` go to both configures. `tools/package.sh build-universal` signs it and makes the disk image.

## Build with Xcode

```sh
cmake -S . -B build-xcode -G Xcode -DJ3D_TEST_DATA=/path/to/data-root
open build-xcode/OpenIndy3D.xcodeproj
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
| `J3D_DEBUG_ASSERTS` | `OFF` | Compiles the engine's debug-only `J3D_ASSERT` checks and debug code (console commands, dumps, `sithWrite.c`'s level writer). Off matches the decomp's release build. `tools/check.sh --asserts` builds and tests it in `build-asserts/` (PORT-PLAN.md P7.3). |
| `J3D_WERROR_SHORTEN` | `ON` | Makes `-Wshorten-64-to-32` an error, in every file since P6.24. |
| `J3D_FIXES` | `ON` | OpenJones3D's fixes to retail bugs (`docs/UPSTREAM-FIXES.md`, P8.9). `OFF` builds retail's behaviour. |
| `J3D_SYSTEM_SDL3` | `OFF` | Link an installed SDL3 instead of building the pinned release; not for distribution. |
| `J3D_ICON_SOURCE` | `platform/apple/bundle/…square.jpg` | The square picture the app icon is made from (the box art, in the repository); empty for no icon. |
| `J3D_BUNDLE_ID` | `io.github.jenavieve-rose.OpenIndy3D` | The app's bundle identifier. |
| `J3D_SANITIZE` | `OFF` | Builds everything, tests included, with AddressSanitizer and UndefinedBehaviorSanitizer. Use a separate build directory (below). |
| `CMAKE_BUILD_TYPE` | none | `Debug` or `RelWithDebInfo` as usual, for the Ninja generator. |

Every compiler warning is an error (`-Werror`), with no exception since P6.24. A clean build prints no compiler or
linker warning (with `J3D_SYSTEM_SDL3=ON` and Homebrew's SDL3, one linker warning per program that links it: built for
a newer macOS than the 13.0 target). The MSVC-dialect warnings the decomp would otherwise raise (missing braces, `sprintf` deprecation
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
| `--level <n>` | Start a new game at level n (1 to 17) without the launcher. |
| `--windowed`, `--fullscreen` | Play in a window or full screen this time. |
| `--help` | Lists the player's options. |
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
- Key sets are chosen, viewed, created and edited in the game's control options (the HUD's options, or the launcher's
  Options, Controls tab), or by editing `Configuration` and the `.kfg` files.

### Sound

Sound plays through the system's default output device and follows it when it changes (headphones in or out).
`Sound Volume=f:<0 to 1>` is the master volume. While the window is in the background the game stops and its sounds
are silent.

**3D sound** is off by default, as in retail: every sound is then placed by the game's own mix (volume and left/right
pan from where it is relative to the camera). `Sound 3D=i:1` turns it on (PORT-PLAN.md decision 19): mono sounds in
the world are placed around the camera with HRTF, which gives them direction on headphones; they fade with distance as DirectSound 3D faded them; and their pitch shifts with doppler as they or the
camera move. Music, interface sounds and stereo sounds stay as they were. Switch it in the sound options (in the game,
or the launcher's Options), in the settings file, or for one run:

```sh
J3D_REG_Sound_3D=1 build/jones3d --data /path/to/data-root
```

| Setting | Effect |
| --- | --- |
| `Sound 3D=i:1` | 3D sound on (default 0). |
| `Sound 3D Rendering=i:<n>` | 0 (default) Apple's best rendering for headphones; 1 HRTF, high quality; 2 HRTF; 3 Apple's rendering for a Mac's built-in speakers (places sounds only slightly apart). 0 to 2 work on any output and are made for headphones. |
| `Sound Head Tracking=i:1` | macOS 15 and later, with rendering 0 and headphones that track the head (AirPods Pro, for example): the sound field stays put as you turn your head. Default 1 (on); also the Sound menu's Head Tracking box. |

The game's "Reverse sound" option swaps left and right in 3D sound. Retail applied it only from a hidden box in the
sound settings dialog and never read it back at startup; the port's sound options leave it out (PORT-PLAN.md P6.15).

`J3D_NO_AUDIO=1` opens no output device: the game runs and keeps its sound state exactly as with one, silently.
`--headless` does the same. `J3D_AUDIO_DUMP=<file.wav>` writes everything the device is given to a 16-bit stereo WAV
at 48 kHz and sends silence to the speakers, for checking the mix without listening:

```sh
J3D_REG_Start_Mode=0 J3D_AUDIO_DUMP=/tmp/level1.wav build/jones3d --data /path/to/data-root --frames 1500 1
```

A null build has no display: without `--headless` it opens the GOBs, loads its string tables, starts the engine and
exits when it enumerates the displays. With `--headless` either build loads the first level and runs:

```sh
build/jones3d --data /path/to/data-root --headless --frames 600
```

Nothing is written into your game folder. Saves (`SaveGames/`), screenshots (`ScreenShots/`) and keysets
(`KeySets/`) go to `~/Library/Application Support/OpenIndy3D/`; the first run copies the retail keysets there.
`JonesError.txt` and `JonesLog.txt` go to `~/Library/Logs/OpenIndy3D/`. Paths may be up to 1023 characters, both
for these folders and for the data root (PORT-PLAN.md P1.30). PORT-PLAN.md tracks what works.

A headless run is deterministic: two runs with the same data and settings print the same log and write
byte-identical saves.

### Indy's outfit

Each level decides whether Indy wears his jacket. **Indy's Outfit** in Game Play Options (in game, or the launcher's
Options) overrides that: *As the Game Chooses* (the default, as retail), *Jacket* or *No Jacket*. The choice is the
`Indy Outfit=i:<0|1|2>` setting and takes effect at the next level start or loaded game; a saved game follows the
outfit chosen when it is loaded. Levels 3, 4 and 11 (`02_riv`, `03_shs`, `10_sea`, where retail always has the jacket)
hold no jacketless model, so there *No Jacket* keeps the jacket. Vehicles (the raft in level 3,
the mine car, the jeep) and the few one-shot cutscene models of Indy are left as they are, except the Peru raft, which
comes in both. For one run:

```sh
build/jones3d --data /path/to/data-root --outfit jacket        # or nojacket, or game
J3D_REG_Indy_Outfit=2 build/jones3d --data /path/to/data-root  # the same as --outfit nojacket
```

### High-resolution textures

A material's textures can be replaced by larger pictures: put a PNG (or any image ImageIO reads) named after the
material in a `hires` folder beside the GOBs, `Resource/hires/<material>.png` for the first cel and
`Resource/hires/<material>_cel<n>.png` for cel `n` (an animated or multi-skin material), where `<material>` is the
`.mat` file's name without `.mat`. A `hires` folder in the `User Path` setting's folder works too. Keep the original's shape (a 64x32 texture as 256x128, say): the game maps textures by fractions of their
size. Alpha is used where the material has it (colour-keyed and translucent textures), and a mip chain is made on
load. `J3D_HIRES_DUMP=<folder>` writes the originals out under those names as a starting point; the setting
`Hires Textures` (launcher Options, Display) turns replacements off.

### Settings and environment variables

Settings are stored in `~/Library/Application Support/OpenIndy3D/settings.ini`, one `<name>=<type>:<value>` per line, using
the value names the retail game kept in the registry. The format is described at the top of
`platform/common/wuRegistry_file.c`.

| Variable | Effect |
| --- | --- |
| `J3D_DATA` | The data root. `--data` sets it. |
| `J3D_SETTINGS` | Path of the settings file to use instead of the default. `/dev/null` runs with defaults and saves nothing. |
| `J3D_REG_<name>` | Overrides one setting for this run; spaces in the name become underscores, for example `J3D_REG_Start_Mode=2`. `J3D_REG_Install_Path=<folder>/` moves saves, screenshots and keysets. |
| `J3D_LOGS` | Folder for `JonesError.txt`, `JonesLog.txt` and `Crash.log` instead of `~/Library/Logs/OpenIndy3D/`. |
| `J3D_TRACE_FILES` | `1` prints every host file that opens successfully to stderr, which shows the loose files overriding GOB contents. |
| `J3D_HEADLESS` | `1` is `--headless`. |
| `J3D_FRAMES` | `<n>` is `--frames <n>`. |
| `J3D_SCREENSHOT_AT`, `J3D_SCREENSHOT_FILE` | `--screenshot-at` and `--screenshot-file`. |
| `J3D_RECORD`, `J3D_REPLAY`, `J3D_INPUT_SCRIPT` | `--record`, `--replay` and `--input-script`. |
| `J3D_NO_AUDIO` | `1` opens no audio output device (sound runs silently). |
| `J3D_AUDIO_DUMP` | `<file.wav>` writes the audio mix to a WAV file and silences the speakers. |
| `J3D_REG_Sound_3D` | `1` turns 3D sound on for the run (it is the `Sound 3D` setting). |
| `J3D_REG_Indy_Outfit` | `0`, `1` or `2`: Indy's outfit for the run, as the game chooses, jacket, no jacket (the `Indy Outfit` setting; `--outfit` sets it). |
| `J3D_INSANE_LOG` | `1` prints the movie player's (INSANE's) messages to stderr, including which movie it plays. |
| `J3D_INSANE_MEMLOG` | `1` prints every allocation of the movie player's debug allocator to stdout. |
| `VSync`, `Frame Limit` (settings) | Vsync on or off (default on) and a frame limit in frames a second (default 0, none), from the launcher's Options (P8.7). |
| `MSAA`, `Anisotropy` (settings) | Multisampling, 1 (off, the default), 2, 4 or 8 samples, lowered to what the GPU supports; anisotropic filtering, 1 (off, the default) to 16. The launcher's Options, Display, "Image Quality" (phase 9); `J3D_REG_MSAA=4` for one run. |
| `Hires Textures` (setting) | `1` (the default) uses high-resolution replacement textures where there are any; `0` ignores them (phase 9, below). |
| `J3D_HIRES_DUMP` | `<folder>` writes every material cel the game loads to the folder as a PNG, named as its replacement would be (an existing file is kept): the starting point for a texture set. |
| `J3D_METAL_MANAGED` | `1` (apple builds) makes CPU-visible textures managed, as an Intel Mac's GPU needs them, on an Apple GPU; the `render-managed` test sets it. |
| `J3D_HEADLESS_SIZE` | `<w>x<h>` (apple builds, headless runs): the desktop size instead of SDL's dummy 1024x768, to render wide pictures on the virtual clock, with `J3D_REG_Width` and `J3D_REG_Height` (P8.9). |
| `J3D_FRAME_STATS` | `1` (apple builds) prints the game loop's frame times at exit: mean, median, 95th and 99th percentiles, worst; presenting and the wait for the display included (PORT-PLAN.md P7.9). |

## Tests

The tests are headless programs that start the engine without display, input, sound output or dialogs and run against
your own game data. The sound module itself runs in them (with no output device), so its state is in every save. They never touch your settings file.

```sh
ctest --test-dir build --output-on-failure        # Ninja tree
ctest --test-dir build-xcode -C Debug             # Xcode tree
```

| Test | What it checks | Time |
| --- | --- | --- |
| `levelload` | Loads the static world and all 17 levels through every binary `.cnd` section loader. | about 40 s |
| `cogcheck` | Compiles every `.cog` in the three GOBs through the engine's lookup, so loose overrides are the files compiled. | about 2 s |
| `saveload` | Runs the game headless for 300 frames, saves, restores the save into a freshly loaded level and compares the world (things, positions, sectors, health, COG globals, game time), saves again and compares the two files, then plays 300 more frames. Saves go to `build/saveload-out/SaveGames/`. | under 1 s |
| `saveload-NN_level` | The same in each of the other sixteen levels (`saveload-02_01_bab` to `saveload-17_17_pru`, numbered in the game's order), each saving under `build/saveload-out/NN/` (PORT-PLAN.md P7.2). | under 1 s each |
| `datacheck` | The game data check (P8.5): made-up folders (no `Resource`, the demo, a missing GOB, sizes known and not, an unknown executable), and with the test data, that it is a tested 1.2 release. | under 1 s |
| `crash` | A headless game sent SIGSEGV mid-level dies of it, leaves `Crash.log` (signal, backtrace, the engine's last output), and the next run reports it once (P8.11). Skipped in sanitizer builds. | about 3 s |
| `determinism` | Starts every level twice, headless for 600 frames: the two runs must print the same log and write byte-identical level-start autosaves (`tests/determinism.sh`). Runs go to `build/determinism-out/`. | about 15 s |
| `boot` | Runs `jones3d --headless --frames 600` (`jones3d-null` in an apple build): level open, the level-start autosave and 20 s of game time in the first level. Must exit 0 with no assert. Its autosave goes to `build/boot/SaveGames/`, not the game folder. | under 1 s |
| `display` | No game data needed. The back buffer, fills, every blit case (8, 16 and 32 bpp, clipped, colour-keyed) against a pixel-by-pixel reference, the RGB565 encoder, a `SaveScreen` BMP, and the conversion of 16-bit material textures (565, 1555, 4444) to the renderer's 32 bpp at full range. | under 1 s |
| `present` | Apple builds only, no game data needed. Presents a pattern through `stdDisplay` in a real window (it flashes up briefly), reads the screen image back from Metal and compares it, unscaled and scaled; then checks that a frame with a 3D scene presents the scene's picture. Skipped without a window server, for example over SSH. | under 1 s |
| `render` | Apple builds only, no game data or window needed. Draws with the Metal renderer offscreen and reads the result back: pixel coverage, colour order, depth test and clears, alpha test and blending, perspective-correct interpolation, depth clamping, lines and points; a diagonal edge, and a texture through a PNG and back (phase 9). `render-msaa` runs it with 4 samples and 16x anisotropy, `render-managed` with managed textures (Intel Macs). | under 1 s |
| `boot-sdl` | Apple builds only: the same run through `jones3d`, so the SDL event loop delivers the frames and the closing `WM_CLOSE` (SDL's `dummy` video driver, no window). Its autosave goes to `build/boot-sdl/SaveGames/`. | under 1 s |
| `keys` | No game data needed. The DIK tables (HID usage to DirectInput key, key names), `GetKeyNameTextA` and `SearchPathA`. | under 1 s |
| `sounddriver` | No game data needed. The sound driver on the virtual clock: one-shots, loops, stop and resume, frequency changes; retail's volume and pan tables; the mix's gains, 8- and 16-bit input, the background mute, rendered without a device; with 3D sound on, retail's channel flags, the inverse distance law, doppler from the source's and the camera's velocity, and head-relative voices; the game's WVSM codec, and the ADPCM block decoder (retail's assembly, translated) bit for bit against an independent decoder. | under 1 s |
| `outfit-01_cyn`, `outfit-03_riv`, `outfit-17_pru` | Indy's outfit (`tests/outfit.c`): starts the level with an outfit (jacket in level 1, none in level 3, which keeps the jacket, and in level 17), checks the local player's and Indy's templates' models, then restores a save of it with each outfit (jacket, none, the game's choice) and checks again, running 300 frames after each. Saves go to `build/outfit-out/`. | about 2 s each |
| `saveload-3d` | `saveload` with 3D sound on, so 3D voices' channel state goes through the save and restore. | under 1 s |
| `audio3d` | Apple builds only, no game data needed; skipped with no audio output device. A tone in a 3D voice at known positions around the listener, captured through `J3D_AUDIO_DUMP` (nothing is heard): right of the camera is louder on the right, left on the left, ahead and behind balanced, a turned listener and "Reverse sound" swap sides as they should. | about 3 s |
| `insaneasm` | No game data needed. The C translations of the movie player's assembler helpers: each pixel converter against a register-level model of its assembler for every 16-bit input and every RGB triple; the copies, fills, table lookups and rectangle blit. | under 1 s |
| `moviecheck` | Decodes every intro movie in `RESOURCE/` headlessly, as fast as it decodes: every picture frame shown, every Blocky16 codec-2 frame equal to a port of ScummVM's decoder, the audio mixed through the movie player's engine and not silent, and the frames' and audio's checksums as pinned. `MOVIECHECK_DUMP=<dir>` writes every 50th frame as a BMP and the audio as a WAV. | about 4 s |
| `launcher` | Apple builds only (`tests/launcher.sh`): Start New Game, Resume Play and Options through the launcher, answered without clicks; skipped with no window server. | about 50 s |
| `dialogs`, `gamedialogs` | The Win32 dialog emulation on the game's templates, and the game's own dialogs through their entry points (Phase 6). | under 1 s |
| `input` | Apple builds only (`tests/input.sh`). Scripted headless runs of level 2 that quicksave at frame 800: holding a key moves Indy; a recorded run replays to a byte-identical quicksave, and recording does not change the run; a scripted controller's A jumps exactly as the jump key does (the port's default key set); with the retail "Microsoft SideWinder game pad" key set its button 7 sidesteps exactly as A does; mouse motion turns Indy. Runs go to `build/input-out/`. | about 10 s |

Each can be run directly, which shows its output:

```sh
build/levelload /path/to/data-root                  # all levels, with section counts per level
build/levelload /path/to/data-root 00_cyn.cnd       # just the levels named
build/cogcheck  /path/to/data-root
J3D_REG_Install_Path=/tmp/j3d/ build/saveload /path/to/data-root 3000 3000      # frames before the save, after the restore
J3D_REG_Install_Path=/tmp/j3d/ build/saveload /path/to/data-root 300 300 14      # in level 14, Nub's Tomb
```

`saveload` needs `J3D_REG_Install_Path` set to a folder it may write to. The second save may differ from the first
only in ways that are retail behaviour (the order of AI status records, animation IDs, and the sound channels' order,
handles and far flag, which retail's restore changes; channels are matched by guid), which the comparison allows and PORTING.md describes ("Savegame round
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
| `tools/check.sh [--data <dir>] [--quick] [--asserts] [tree ...]` | The whole check, before every commit: configures, builds and tests the apple, null and sanitizer trees (and with `--asserts` the debug-asserts tree in `build-asserts/`), fails on any compiler or linker warning but SDL3's, and runs the layout audit (PORT-PLAN.md P7.1). `--quick` checks the apple tree only. About 15 minutes for all trees. |
| `tools/level_shots.sh <data-root> <out-dir> [frames]` | Screenshots of all 17 levels, headless, at the given frames (default `1,300`). Needs an apple build in `build/`. |
| `tools/level_sessions.sh <jones3d> <data-root> <out-dir> [frames]` | A headless session in every level with any build's game binary (default 9000 frames, five minutes of game time): reports asserts, sanitizer reports, an idle Indy's death, the run time and peak memory (PORT-PLAN.md P7.3, P7.9). |
| `tools/package.sh [<build-dir>] [<out-dir>]` | Signs `OpenIndy3D.app` with the Hardened Runtime, packs it in `dist/OpenIndy3D-<version>.dmg` with the README and a link to Applications, and signs that. `J3D_SIGN_IDENTITY` is the Developer ID (default an ad-hoc signature, for this Mac only); `J3D_NOTARY_PROFILE`, a `notarytool store-credentials` profile, notarizes and staples the image (P8.13). It warns when the icon is the retail box art. |
| `tools/make_icns.sh <picture> <out.icns>` | Makes the app icon from a square picture (the build runs it). |
| `python3 tools/gen_null_platform.py` | Regenerates `platform/common/null/*_null.c` from the public headers of the replaced modules. Run it after changing the script's overrides or its `IMPLEMENTED_ELSEWHERE` set. It stops if a Win32 function is declared that nothing implements (PORT-PLAN.md P7.7). |

## Before committing

```sh
tools/check.sh --data /path/to/data-root
```

It needs the game data for the tests that use it; without `--data` it takes `J3D_TEST_DATA` or the trees' CMake caches.

Record any change to the imported sources in [PORTING.md](PORTING.md).
