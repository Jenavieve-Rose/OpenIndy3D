# Indiana Jones and the Infernal Machine: Apple Silicon Port Plan

Revision 2, 2026-11-12, updated the same day when phase 1 closed, and since as each phase closes (phase 4 on
2026-11-13). Replaces draft 1 of 2026-11-11 (still readable with
`git show 8e9a940:PORT-PLAN.md`).

This revision records what has been built since draft 1, splits every phase into steps small enough to finish and
verify one at a time, and turns the plan into a checklist. Section 5 is the checklist; the table below is its summary.

Section 3 (decisions), section 7.1 (floating point) and the phase numbers are referenced from `CMakeLists.txt`,
`docs/PORTING.md` and source comments, so they keep their numbering. Steps are named `P<phase>.<n>` so they cannot be
confused with section numbers.

## Progress at a glance

| Phase | Outcome | Steps done | State |
| --- | --- | --- | --- |
| 0 | Tree compiles and links on arm64 with a null platform | 12 of 12 | Complete |
| 1 | Headless boot, every level loads, 64-bit audit closed | 30 of 30 | Complete |
| 2 | Window, 2D back buffer and Metal renderer | 22 of 22 | Complete |
| 3 | Keyboard, mouse and gamepad input | 10 of 10 | Code audit done; exit test is a playthrough by hand |
| 4 | Audio | 11 of 11 | Code audit next; exit test needs a listening pass by hand |
| 5 | Movies (INSANE) | 0 of 12 | Not started |
| 6 | Dialogs: options, save and load, store, controls | 0 of 25 | Not started |
| 7 | Full-game verification, shim removal | 0 of 10 | Not started |
| 8 | Signed `.app`, data locator, fixes and polish | 0 of 14 | Not started |
| **All** | | **85 of 146** | |

Step counts are not effort: the open steps in phases 2 and 6 are much larger than the closed ones in phases 0 and 1.
Open steps carry a size, **S** (hours), **M** (a day or two) or **L** (several days), for one engineer.

### What works today (measured 2026-11-13 on the working tree)

| Check | Result |
| --- | --- |
| `cmake --build` from clean, arm64, Apple clang | Builds and links with `-Werror`, exit 0. 58 compiler warnings, all known: the narrowing conversions in `jonesConfig.c` (P6.24); in the default `J3D_PLATFORM=apple` tree, the same linker warning about Homebrew's SDL3 deployment target for each of the three programs that link SDL3 (P8.3) |
| `ctest` | `display`, `keys` and `sounddriver` pass (no game data needed), and in the apple build `render` and `present` (`present` needs a window server). With `-DJ3D_TEST_DATA`: `levelload`, `cogcheck`, `saveload` and `boot` pass, and `boot-sdl` and `input` in the apple build |
| The same under `-DJ3D_SANITIZE=ON` (ASan and UBSan) | All pass with no report; so does a 3,000 + 3,000 frame `saveload` run |
| `levelload <data>` | Static world and all 17 levels load, 0 failed, section counts plausible |
| `cogcheck <data>` | 1,132 scripts compiled, 0 failed |
| `python3 tools/layout_audit.py` | `0 retail on-disk type(s) differ` |
| `jones3d --data <data>` | Apple build: opens the game window (fullscreen or windowed, at the mode the settings name) and runs the game in it, rendered through Metal at the display's refresh, played with keyboard, mouse and controllers, with sound through the default output device. Null build: stops at the display enumeration (no display backend) |
| `jones3d ... --input-script` / `--record` / `--replay` | Scripted keyboard, mouse and controller input reaches the game; a recorded run replays to a byte-identical quicksave (`input` test) |
| `jones3d --data <data> --headless --frames 600` | Loads `00_cyn`, writes the level-start autosave, runs 600 frames (20 s of game time) of the opening cutscene and shuts down cleanly, exit 0, in under a second. Two runs print the same log and write byte-identical saves |
| `saveload <data>` | After 300 frames, a save restored into a fresh world matches the world that was saved (236 things, positions bit for bit, COG globals, game time), and a save of the restored game matches the first; the game then runs 300 more frames |
| Long paths | `boot` and `saveload` pass with a 257-character `Install Path`; `boot`, `levelload` and `cogcheck` with a 229-character data root |

Every level renders in a window or fullscreen through Metal: textured, lit, fogged, with the HUD, at the display's
refresh rate, pillarboxed to 4:3 by default, and plays with the keyboard, the mouse and controllers, using the retail
key sets or the port's controller default (phase 3), with music, ambience, effects and voices placed by the game's
own camera-relative mix (phase 4). Movies do not play (phase 5) and the menus and dialogs do not appear (phase 6);
the in-game inventory menu works.
`tools/level_shots.sh` screenshots every level by script.

All of phase 1 is on `main` (at `3c3605a`): sixteen commits made on the branch `phase1-headless`, seven for P1.8 to
P1.18 (`28e2120` to `b4b56e7`) and nine for P1.19 to P1.30, the audit and the docs (`f0d0935` to `3c3605a`), merged
by fast-forward on 2026-11-12. Each builds and passes its tests; before `5ba51d2` (P1.30) the `boot` test needs a
build tree with a short path.

Phases 2 and 3 are on `main` (at `b0979f2`), merged by fast-forward on 2026-11-13: fourteen commits from the branch
`phase2-display`, thirteen for P2.1 to P2.22 (`b872038` to `ec42ba4`) and the audit (`22a2f9b`), then two from
`phase3-input`, P3.1 to P3.10 (`250a7a1`) and the audit (`b0979f2`). At `b0979f2` everything builds and every test
passes in the apple, null and sanitizer trees; the commits before it were not each rebuilt for the merge.

## 1. Goal

A native arm64 macOS application that plays the full retail game (all 17 levels: 16 story chapters and the bonus level,
with cutscene movies, saving and loading, the in-game menus and store) from the player's own copy of the game data, with
no Windows executable, Wine, Rosetta, or DLL injection involved.

Non-goals for v1.0: multiplayer (the game has none), the level editor paths, savegame compatibility with Windows saves,
Intel Mac support (cheap to add later as a universal binary, but not a milestone).

## 2. What we have to work with

Paths are relative to `~/Developer/Indy3D_Mac`.

| Item | Location | What it is | Role |
| --- | --- | --- | --- |
| The port | `j3dport/` | Git repository, hard fork of the decomp at upstream `319a6fc2`; phases 0 and 1 are on `main` | **Where all work happens.** |
| Byte-exact decompilation | `infernal-machine-decomp-public-main/` | 100% source for `Indy3D.exe` v1.2 and v1.0, ~188k lines of C, plus zlib 1.0.9 and the INSANE movie player | Pristine upstream copy, for diffing and cherry-picks. Its docs are imported into `j3dport/docs/`. |
| OpenJones3D v0.3.1 | `OpenJones3D-main/` | Partial reimplementation (87%) that runs by injecting a DLL into the retail exe | Not a base. Source of ~80 catalogued fixes (`CHANGELOG.md`), applied in phase 8. |
| Retail game data | `Indy3D/` | `RESOURCE/` with `CD1.GOB`, `CD2.GOB`, `JONES3D.GOB`, five `.snm` intro movies, 51 patched `.cog` scripts in `Cog/`; `KEYSETS/*.kfg` | Complete v1.2 install (`INDY3D.EXE` = `55fb00c0…`). The test data root: `-DJ3D_TEST_DATA=…/Indy3D`. |
| OpenAL Soft | `openal-soft-master/` | Source tree, not referenced by the build | Fallback audio backend only (decision 7). |
| SDL3 | Homebrew, 3.4.20 | Installed on the development machine, not yet referenced by the build | Window, input, gamepads from phase 2. |
| miniaudio 0.11.25 | `j3dport/third_party/miniaudio/` | Vendored single-file audio library (public domain or MIT-0) | The output device from phase 4 (decision 7). |

Both source trees are AGPL v3. The port inherits that license. No game assets may be shipped; the app must locate the
player's own data.

### 2.1 Where the Windows dependence lives, and where each module stands

Line counts were measured on the decomp tree for draft 1.

| Module | Lines | Windows exposure | State in the port |
| --- | --- | --- | --- |
| `Libs/sith` (game logic, COG VM, AI, physics) | 104,110 | A handful of stray calls (registry, GDI thumbnail in savegame, console) | Compiles with `-Werror`. Level loaders audited and fixed; narrowing triaged; sanitizer-clean on the tested paths. |
| `Libs/rdroid` (renderer front end, math) | 19,762 | `LPD3DTLVERTEX` in one struct, the SSE "Katmai" mesh path | Compiles with `-Werror`. Katmai sources dropped, and since P2.21 their null stub and call sites too. |
| `Libs/std/General` | 5,781 | x87 inline asm, backslash paths | Done: x87 compiled out, both path separators accepted. |
| `Libs/std/Win95` | 9,768 | `std3D` (Direct3D 6), `stdDisplay` (DirectDraw), `stdControl` (DirectInput), `stdComm` (DirectPlay), console, window store | Excluded from the build. Generated null backends stand in. `stdGob.c` kept. System-memory VBuffers are real. |
| `Libs/sound` | 6,713 | `Driver.c` is DirectSound; `Sound.c` and `AudioLib.c` are portable apart from the buffer handle type and one asm loop | Phase 4: `Driver.c` replaced by `platform/common/SoundDriver_mix.c` over a miniaudio device; the handles are opaque; the asm loop's C translation verified (P4.8). Save and restore run with sound open. |
| `Libs/smush` (INSANE) | 10,267 | 6 MASM files (1,815 lines), `Dsnd.c`, `Timer.c`, `Stdfile.c`, `Stdio.c` | OS modules register nothing; the 14 asm entry points abort if reached. No movies. File handles and Blocky16 frame offsets made 64-bit clean in phase 1. |
| `Libs/wkernel`, `Libs/w32util` | 692 | 100% | `wkernel` is a hand-written null (message queue, quit). `wuRegistry` is done (`settings.ini`). |
| `Jones3D/` (game shell) | 23,318 | `jonesConfig.c` (9,955 lines of dialog procedures), `JonesDialog.c`, `JonesDevDialog.c`, `Indy3D.rc` (26 dialog templates) | Compiles against the shim headers. Every dialog call returns failure. `jonesConfig.c` keeps 58 narrowing warnings until P6.24. |
| `Libs/zlib` | 7,512 | None | Vendored and built (it is modified to read through INSANE's file table). |

Facts that shape the remaining design:

- The GPU is only a rasterizer. `rdroid` transforms, clips, lights and projects on the CPU and hands
  `std3D_DrawRenderList` pre-transformed triangles (screen x, y, z, 1/w, diffuse, specular, u, v) with a texture and a
  small render-state bitmask. The whole `std3D` interface is 33 functions.
- 2D (fonts, console, dialogs, movie frames) writes pixels into a locked back buffer. The port must provide a CPU-side
  back buffer that is composited onto the Metal drawable each frame. `stdDisplay` is 41 functions, 6 of which exist.
- Key bindings are stored as DirectInput `DIK_*` scancodes and the `.kfg` keyset files name keys by DirectInput names.
  The input layer must produce DIK codes. `stdControl` is 30 functions.
- `SoundDriver` is 36 functions, a thin DirectSound buffer wrapper with 3D positioning.
- `platform/common/null/win32_null.c` holds 254 Win32 stubs. That list is the exact surface the dialog work (phase 6)
  must either implement or prove dead.
- The dialog templates use buttons, statics, 14 combo boxes, 3 list boxes, 5 list views, 4 trackbars, one edit control
  and one scroll bar. `Indy3D.rc` also names 43 icon and bitmap files that are **not** in the repository: they exist
  only inside the retail executable (see P6.10 and risk 7.3).
- Textures are converted to the backend's format when a level loads, so even the null renderer names a format. It
  reports 32-bit BGRA, the layout the Metal backend will upload.

## 3. Strategy decisions

Decisions 1 to 11 are unchanged from draft 1. Their status is noted where work has confirmed or exercised them.

1. **Base the port on the decomp, not on OpenJones3D.** Done.
2. **Hard-fork into a new repository with the same file layout.** Done: `j3dport/`, upstream commit recorded in
   `NOTICE` and `docs/PORTING.md`. LucasArts' module and function names are kept.
3. **64-bit arm64 native from day one.** Holding: all 17 levels load on arm64 after the fixes listed in
   `docs/PORTING.md`.
4. **Replace, do not wrap, the platform layer.** Keep the public headers of `std3D`, `stdDisplay`, `stdControl`,
   `SoundDriver`, `wkernel`, `wuRegistry` and write new implementations under `platform/`. In place: the Windows
   sources are excluded in `CMakeLists.txt` and `platform/` supplies every function.
5. **Graphics: Metal directly**, through a small Objective-C file with a C interface. SDL3's GPU API is the fallback if
   Linux or Windows becomes a goal, so the Metal calls stay in one file.
6. **Window, input, timing, gamepads: SDL3.**
7. **Audio: miniaudio** (single-file C, Core Audio backend, built-in spatializer). OpenAL Soft is the alternative if
   the spatialization sounds wrong; its source is already in the workspace. In place (phase 4), for the output device
   only: the mixing is the port's own, and no spatializer is needed (decision 19).
8. **Movies: port INSANE, do not swap in ScummVM.** The six assembler files become C. ScummVM's `Blocky16` codec
   (GPLv2+) is the reference for the hardest one.
9. **GUI: emulate the slice of the Win32 dialog API that `jonesConfig.c` uses**, backed by our own widgets drawn with
   the engine's font and 2D primitives, so ~10k lines of dialog logic stay intact.
10. **Build with CMake; generate the Xcode project from it.** In place (`build/` with Ninja, `build-xcode/`).
11. **Bring over OpenJones3D's fixes as a curated patch series, after the game runs**, so "port bug" and "behaviour
    change" stay distinguishable.

Decisions made while doing phases 0 and 1:

12. **Every change to imported code is marked and logged.** A `// Port:` comment in the source, `#ifdef J3D_PORT` where
    the retail body is kept beside the new one, and an entry in `docs/PORTING.md`.
13. **Null backends are generated, and replaced one module at a time.** `tools/gen_null_platform.py` writes a stub for
    every prototype in the replaced headers. A real implementation lands by listing its functions in
    `IMPLEMENTED_ELSEWHERE` (or dropping the header from the generator), so the tree links at every step.
14. **Tests are headless programs that link the game as a library** (`jones3d_game`) and run against the player's own
    data through `-DJ3D_TEST_DATA`. No asset is copied into the repository.
15. **The port does not run from the data folder.** `Data Root` (from `--data`, `J3D_DATA` or `settings.ini`) points at
    the folder holding `RESOURCE/`; loose files there override GOB contents as they did for the retail exe.
16. **Savegames use the port's native layout.** `NdsHeader` is 1,320 bytes on arm64 against 1,188 in retail. That is
    accepted (non-goal in section 1) and covered by a round-trip test instead of a fixed layout.
17. **Every warning is an error.** Phase 1 closed the backlog; `-Werror` now applies to the whole tree, with two
    named exceptions that later steps remove (P4.8, done; P6.24). A narrowing that retail computed in 32 bits is written as
    an explicit cast marked `// Port: narrowing cast`; one that was wrong on 64-bit is fixed by type.
18. **Undefined behaviour gets a defined value, chosen from the code's evident intent.** Where retail read an
    uninitialised local or wrote past an array and only worked by accident, the port picks the value the surrounding
    code implies (or OpenJones3D's, where it fixed the same thing), logs it in `docs/PORTING.md`, and leaves anything
    that changes intended behaviour to the phase 8 fix series. Headless runs are deterministic, so a change in
    behaviour shows up as a change in the `boot` log or the saves.
19. **Sound is retail's software path, kept on the engine clock** (phase 4). The driver reports no 3D sound hardware,
    as a retail machine without it did, so every sound is placed by the game's own camera-relative mix and the options
    dialog greys the 3D check box; DirectSound 3D is not emulated (docs/PORTING.md, "The sound driver", for why).
    Everything the sound module can observe of a voice is computed from the engine clock, never from the device, so
    headless, recorded and replayed runs keep byte-identical saves and the null build runs the same driver silently.

## 4. Target architecture

```
Jones3D app shell (JonesMain, HUD, inventory, dialogs' logic)      imported C, builds today
        │
sith (game) ── rdroid (renderer front end) ── sound/Sound.c        imported C, builds today
        │              │                          │
   platform/                                      │
   ├─ common/crt_posix.c, host_path.h   MSVC CRT names, path conversion      exists
   ├─ common/host_clock.h, host_dirs.c  virtual clock, user and log folders  exists
   ├─ common/stdDisplay_common.c        display queries, modes, back buffer  exists
   ├─ common/null/stdDisplay_headless.c in-memory display for tests          exists
   ├─ common/wuRegistry_file.c          settings.ini                         exists
   ├─ common/stdDisplay_vbuffer.c       system-memory VBuffers               exists
   ├─ common/null/*_null.c              generated null backends              exists, shrinks every phase
   ├─ apple/main.c                      entry point, --data                  exists
   ├─ apple/shim/                       Win32 and DirectX type headers       exists, deleted in phase 7
   ├─ apple/wkernel_sdl.c               event loop, window proc messages     exists
   ├─ apple/stdDisplay_sdl.c            display device, modes                exists
   ├─ apple/display_metal.m             back-buffer present over Metal       exists
   ├─ apple/std3D_metal.c, render_metal.m  Metal renderer                    exists
   ├─ apple/stdControl_sdl.c            keyboard to DIK, mouse, gamepads     exists
   ├─ common/SoundDriver_mix.c          voices on the engine clock, mixer    exists
   ├─ apple/audio_device_ma.c           miniaudio output device              exists
   ├─ insane_os/                        timer, file I/O, audio sink, asm→C   phase 5
   ├─ j3dgui/                           Win32 dialog emulation and widgets   phase 6
   └─ apple/ (bundle, data locator)                                          phase 8
```

Threading stays as the original: one game thread, INSANE's timer thread, and the audio callback thread.

## 5. Work breakdown and checklist

`[x]` is done and verified. `[ ]` is open. A step is only ticked when its check has been run; the check is named in
the step or in the phase's exit test. Work phase by phase, top to bottom, unless a step says it can run in parallel.

### Phase 0: Bootstrap (complete)

Exit test, passing: `jones3d` links on arm64, prints the engine's startup log and stops at display open.
All of it is in commit `2d8bbed` (import in `cd15970`).

- [x] **P0.1** Create the port repository and import the decomp at `319a6fc2`.
- [x] **P0.2** Add `LICENSE` (AGPLv3) and `NOTICE` crediting LucasArts, the decomp, OpenJones3D, OpenJKDF2 and zlib.
- [x] **P0.3** CMake project with targets `zlib`, `std`, `rdroid`, `sound`, `smush`, `sith`, `platform`, `jones3d`;
      C11, `-fno-strict-aliasing -fwrapv -ffp-contract=off`, pointer/integer cast warnings as errors.
- [x] **P0.4** Replace `j3dcore/types.h` with `<stdint.h>`/`<stddef.h>`; remove the `CF_*` count-fit macros and blocks.
- [x] **P0.5** Replace the `_WIN32` branch of `j3d.h.in` (`J3DAPI`, `J3DNORETURN`, case-insensitive compares);
      `__inline` becomes `static inline`.
- [x] **P0.6** Win32 shim headers in `platform/apple/shim/` (2,517 lines in `win32_shim.h`, `win32_shim_dx.h`,
      `win32_shim_gui.h`). Tracked as debt; removed in phase 7.
- [x] **P0.7** Exclude the Windows platform sources from the build; drop the Katmai mesh path and `sithWrite.c`.
- [x] **P0.8** Null platform: `tools/gen_null_platform.py`, hand-written `win32_null.c` and `wkernel_null.c`, and
      real POSIX bodies for the MSVC CRT names in `crt_posix.c`.
- [x] **P0.9** Fix every pointer-to-`int` truncation needed to compile (table in `docs/PORTING.md`).
- [x] **P0.10** Compile out the x87 control-word code; build with `NONMATCHING_MATH`.
- [x] **P0.11** `platform/apple/main.c`: rebuild the command line and call the game's `WinMain`.
- [x] **P0.12** Generate an Xcode project from the same CMake files (`build-xcode/`).

### Phase 1: Headless boot and the 64-bit audit (complete)

Exit test, passing: `ctest` runs `levelload`, `cogcheck`, `boot` and `saveload` green; `layout_audit.py` reports 0;
the same tests pass under AddressSanitizer and UBSan; the tree builds with `-Werror` for return-type, format and
64-to-32 narrowing (all warnings are errors; `jonesConfig.c`'s narrowing waits for P6.24).

#### 1A. Settings, paths, on-disk layouts (complete, committed)

- [x] **P1.1** `wuRegistry_file.c`: settings in `~/Library/Application Support/J3DPort/settings.ini`, same value
      names as the registry, `J3D_SETTINGS` and `J3D_REG_<name>` overrides. (`ddf3011`)
- [x] **P1.2** `Data Root` from `--data`, `J3D_DATA` or the file; default for `Install Path`, `Source Dir` and
      `User Path`. (`ddf3011`)
- [x] **P1.3** `stdFnames` accepts `\` and `/` and builds with `/`; `stdGob_FileOpen` folds back to `\` for its
      directory lookup. (`ddf3011`)
- [x] **P1.4** `J3DHost_ToPosixPath`: one engine-to-host path conversion with case-insensitive component matching,
      used by `stdFileOpen` and every path call in `crt_posix.c`. (`ddf3011`)
- [x] **P1.5** Route the engine's three direct `fopen` sites through `J3DHost_fopen`. (`ed23c42`)
- [x] **P1.6** `tools/layout_audit.py` and fixed-width on-disk records with `_Static_assert` sizes: `CndWorld`, thing,
      AI and particle headers, surface and material counts, PVS size, `SoundInfoFile`. (`8e9a940`)
- [x] **P1.7** Text parsers: 24 `sscanf` conversions from `%d` to `%zd`; four memory-safety fixes found on the way.
      (`8e9a940`)

#### 1B. Test harness and loaders (complete, committed)

- [x] **P1.8** `tools/gobtool.py`: list and extract GOB contents.
- [x] **P1.9** `stdDisplay_vbuffer.c`: system-memory VBuffers, and a null `std3D` that names a texture format, so
      materials and fonts load with no display.
- [x] **P1.10** Loader fixes found by the level test: index arrays allocated at `sizeof(size_t)` and converted
      element by element (sectors, surfaces, model faces); material loader allocates a buffer per cel and mip.
- [x] **P1.11** Loose-file override root is the data root's `RESOURCE/` (retail relied on the working directory);
      `J3D_TRACE_FILES=1` logs host file opens.
- [x] **P1.12** `tests/levelload.c`: loads the static world and all 17 levels through every binary section loader and
      prints section counts. Passing, 0 failed.
- [x] **P1.13** `tests/cogcheck.c`: compiles every `.cog` in the three GOBs through the normal lookup, so loose
      overrides are the ones compiled. CMake target and `ctest` entry added. Passing: 1,132 scripts, 0 failed.
- [x] **P1.14** `levelload.c` uses the shared `tests/harness.c` for engine startup and crash handlers; its output is
      unchanged. `CMakeLists.txt` builds every name in `J3D_TESTS` the same way, so a new test is one line.
- [x] **P1.15** 1B and the headless work (P1.17, P1.18) are committed as seven reviewable commits on the branch
      `phase1-headless`; each builds on its own. The branch, with the rest of phase 1, is merged into `main`.
- [x] **P1.16** `docs/BUILDING.md` is the port's build guide: Ninja and Xcode configure lines, CMake options, the data
      root, environment variables, tests and tools. The decomp's guide is kept as `docs/DECOMP-BUILDING.md`.

#### 1C. Run the game loop headless (complete)

The game has no title screen: with `Start Mode` 0 it loads a level and enters the game loop. These steps make the
loop itself testable before any renderer exists.

- [x] **P1.17** Headless display: with `--headless` (`J3D_HEADLESS=1`) `stdDisplay_headless.c` provides one device, one
      640×480 mode and a system-memory back buffer, the null `std3D` starts and opens, and time runs on a virtual
      clock that advances 33 ms per presented frame. The game opens its display, loads `00_cyn` and runs its loop.
      Getting there needed five fixes outside the display, all in `docs/PORTING.md`: two crashes in the movie player
      when it has no backend, memory bitmaps for the dialog icons and the savegame thumbnail, a savegame writer that
      fell off its end without a return value, and the level-start autosave racing the opening script.
- [x] **P1.18** `--frames <n>` (`J3D_FRAMES`) ends the run after n passes of the game loop through the normal
      shutdown. `ctest` runs `jones3d --headless --frames 600` as the `boot` test; it passes in about 0.3 s.
- [x] **P1.19** The game no longer writes into the player's game folder. `Install Path`, under which the engine puts
      `SaveGames/`, `ScreenShots/` and `KeySets/`, defaults to `~/Library/Application Support/J3DPort/` (beside
      `settings.ini`); on first run the retail keysets are copied into its `KeySets/`. `JonesError.txt` and
      `JonesLog.txt` go to `~/Library/Logs/J3DPort/` (`J3D_LOGS` overrides). The app entry point prepares the folders,
      so the test programs never write under the user's Library; the `boot` test points both at the build tree.
- [x] **P1.20** `tests/saveload.c`: runs the game headless, saves after 300 frames, restores into a fresh world and
      compares things (index, type, guid, signature, flags, position bit for bit, sector, health), the COG globals,
      game time and the player; saves the restored game and compares the files message by message; plays 300 more
      frames. Registered with `ctest`. Two headless runs now write byte-identical saves: the differing bytes were stale
      pointer bits in the COG-global copy, a heap pointer written into sprite records (a retail writer/reader mismatch
      that also gave restored sprites a garbage alpha), and absolute times from a virtual clock that started at the
      real uptime. Three differences between a save and the save of its restore are retail behaviour (AI block order,
      animation IDs renumbered on restore, `Sound_Restore`'s pause time) and are allowed by name; see
      `docs/PORTING.md`, "Savegame round trip". Other levels in P7.2.
- [x] **P1.21** `-DJ3D_SANITIZE=ON` (ASan, UBSan; `docs/BUILDING.md`). All four tests pass under it. Fixed what it
      reported: zone-allocator alignment, the debug allocators' guard words (INSANE's overran its block on 64-bit),
      an out-of-bounds write in `sithEvent`, a read before `stdConffile_aLine`, a read past the face cache in
      `rdCache`, the COG lexer indexing its class table with a negative `char`, NULL arithmetic in 30 sound loops, and
      unaligned DSS message fields. Table in `docs/PORTING.md`, "Sanitizer findings".
- [x] **P1.30** Path length. `J3D_PATHSIZE` (1024) for every buffer that holds a host path: the save, screenshot,
      keyset, log and resource paths, the three path-size constants, the GOB and find-file structures, and the
      `stdConffile` filename stack. Tested with a 257-character `Install Path` and a 229-character data root. The file
      dialogs in `jonesConfig.c` keep 128 bytes until P6.17/P6.24.

#### 1D. Close the warning backlog (complete)

Counts are from a clean build on 2026-11-12, before the work.

- [x] **P1.22** 19 non-void functions that could fall off their end (`-Wreturn-type`: 10 in `sith`, 5 in
      `Jones3D/Display`, 2 in `rdroid`, 1 in `JonesDialog.c`, 1 in INSANE's `Zfilio.c`). Each returns what its callers
      need, judged from what retail's `eax` held (table in `docs/PORTING.md`, "Functions with no return value").
      `-Werror=return-type` is on.
- [x] **P1.23** 58 `-Wformat` warnings fixed (`%zu`/`%zd` for `size_t`, `%p` for pointers, a debug trace that printed
      `int`s with `%g`, two `sscanf` targets). Covered by `-Werror`.
- [x] **P1.24** MSVC's `rand()` (15-bit LCG, seed 1) in `crt_posix.c`, mapped by `j3d.h`, with `RAND_MAX` 0x7FFF: the
      engine draws retail's sequence, and the 144 `RAND_MAX` conversion warnings are gone.
- [x] **P1.25** `-Wshorten-64-to-32` in `std` (18), `rdroid` (61) and `sound` (19): explicit casts where retail's
      32-bit value is exact; the face-sort comparator no longer narrows a pointer difference between two worlds.
- [x] **P1.26** `-Wshorten-64-to-32` in `sith` (283): explicit casts (counts, indices, in-array pointer differences,
      `lrintf`), the DSS length macro, and the flex scanner's macros. None was a truncated pointer.
- [x] **P1.27** `Jones3D/Main`, `Gui`, `Play` (22), `smush` (24), `zlib` (3), and `JonesHud.c` (22, not deferred after
      all). Real 64-bit bugs: INSANE kept its file handles in `int` (movies would have crashed in phase 5), Blocky16
      frame offsets between separate allocations were `int` (now `ptrdiff_t`), and the HUD's millisecond timers were
      `size_t` (now `uint32_t`, wrapping like `timeGetTime`). The 58 in `jonesConfig.c` wait for P6.24.
- [x] **P1.28** The other 52: format strings that were messages, include-path case, constant truncations,
      qualifiers, enum pointers, `p = p = next`, address-of-array tests, `abs` variants, a handle compared with
      `NULL`. `AudioLib.c:1703` stays for P4.8. Details in `docs/PORTING.md`, "Compiler warnings".
- [x] **P1.29** `J3D_WERROR_SHORTEN` is on by default, and every other warning is an error too (decision 17), with
      `jonesConfig.c` exempt from the narrowing error and `-Wunsequenced` from the general one.
## Complete Phase 1 Code Audit

Done 2026-11-12; details in `docs/PORTING.md`, "Phase 1 code audit". The phase's code was rebuilt with the warnings
the build does not enable (`-Wall -Wextra`, the uninitialised-use family, `-Wcast-align`,
`-Wcast-function-type-mismatch`), and the code written in phase 1 that had not been reviewed was read through.

- [x] Uninitialised reads reached by the game now have defined values (decision 18): the final push or pull of a
      pushable item moved along an unset vector (now `direction3`; verify in P7.5), underwater sounds had unset
      environment flags, torso fire rotated by an unset vector, the mine-car AI and a snake path test read unset
      locals, push/pull scrape sounds had no mode on plain surfaces, raft boarding read an unset flag, and the
      thumbnail error path deleted unset handles.
- [x] The eight function-pointer casts with mismatched return types are safe on arm64 (result discarded or
      narrowed at once). The 188 `-Wcast-align` casts are on paths phases 2, 4 and 5 bring to life; those phases run
      under the sanitizer build.
- [x] Behaviour check: the `boot` log is unchanged by the warning work; the audit's changes are on paths the
      opening of `00_cyn` does not reach.
- Left open, each with its phase: `jonesConfig.c` (P6.24); `moveRate` and `dlgResult` in `JonesHud.c` and
  `bFinished` in `JonesMain.c` (unset on paths that need a display or input); INSANE's `rc` (phase 5); the retail
  save/restore differences listed under P1.20 (phase 8 fix series, and P4.9 for the sound pause time).

### Phase 2: Display and renderer

Exit test: `00_cyn` renders correctly in a window at native resolution, a screenshot of the first frame of every level
can be produced by script, and `stdDisplay_SaveScreen` works.

#### 2A. Window and 2D

- [x] **P2.1** (S) Build plumbing: `find_package(SDL3)`, add `OBJC` to the project languages, link Metal, QuartzCore
      and Cocoa. Add a `J3D_PLATFORM` switch (`null` or `apple`) so the headless tests keep the null backends.
      Done: `platform` split into `platform_core`, `platform_null` and `platform_apple`; tests and the new
      `jones3d-null` (run by `boot`) link the null backends; `host_apple.m` logs the host, Metal device and SDL
      version. Checked: apple, null, sanitizer and Xcode trees build; all four tests pass in apple, sanitizer and null
      trees (`docs/PORTING.md`, "Phase 2").
- [x] **P2.2** (M) `wkernel_sdl.c`: create the window, implement `wkernel_Run`, `wkernel_PeekProcessEvents` and
      `wkernel_ProcessEvents` over the SDL event queue, and deliver the window messages that `JonesMain_GameWndProc` and
      `JonesMain_IntroWndProc` handle (activate, close, size, paint, key and char messages). Check: an empty window
      opens and closes cleanly. Done: also mouse messages and the game window's `PostMessage` and timers; `--headless`
      uses SDL's dummy driver and the new `boot-sdl` test runs the event loop. Checked: a window opens and a quit
      event closes it with exit 0 (also under the sanitizers); `boot-sdl` matches `boot`'s log and save. Focus loss
      and keys are checked by review only until scripted input exists (Phase 3).
- [x] **P2.3** (M) `stdDisplay_sdl.c`, device and modes: `Startup`, `Open`, `Close`, one display device, the mode list
      (desktop modes plus window sizes, within the engine's 64-mode cap), `SetMode`, `FindClosestMode`,
      `GetCurrentVideoMode`, `IsFullscreen`, `CanRenderWindowed`. Done: the backend-independent half of `stdDisplay`
      (queries, `FindClosestMode`, the back buffer) moved to `stdDisplay_common.c`, shared with the headless device;
      fullscreen covers the display at the desktop mode with no mode change; the null `std3D` builds its display
      environment from `stdDisplay` as retail did, and the game's mode search takes 32 bpp modes, so the `Width` and
      `Height` settings apply. Checked: all tests pass in apple, null and sanitizer trees, `boot-sdl` and `boot` saves
      still identical; windowed 800x600 and default fullscreen verified on screen (`docs/PORTING.md`, "Display device
      and modes").
- [x] **P2.4** (S) Back buffer: a 32-bit BGRA system-memory VBuffer as the engine's back buffer;
      `LockBackBuffer`/`UnlockBackBuffer`, `BackBufferFill`, `ClearRect`, `GetBackBufferSize`. Done: shared by both
      backends in `stdDisplay_common.c`; return values as retail. Checked: `display` test.
- [x] **P2.5** (M) `stdDisplay_VBufferFill` and `stdDisplay_VBufferCopy` with colour key: port the retail
      system-memory blit paths for 32 bpp only; `EncodeFromRGB565`. Done: `BlitSysToSys` for 8, 16, 24 and 32 bpp, with
      a signed clip against both buffers (retail could read and write out of bounds). Checked: `display` test, every
      overhang and key case against a pixel-by-pixel reference.
- [x] **P2.6** (M) First present path: upload the back buffer to a Metal texture and draw it as a full-screen quad to
      the `CAMetalLayer` drawable in `stdDisplay_Refresh`, with vsync. Check: the level-load progress screen is
      visible. Done: `display_metal.m`, called from `stdDisplay_Update` (retail's flip; `Refresh` re-sets a mode);
      a ring of three textures, bilinear scaling to the window, vsync. The load screen is drawn through `std3D`, so it
      appears with P2.9 to P2.12; checked instead by the new `present` test, which reads the drawable back and compares
      it with a pattern drawn through `stdDisplay`, unscaled and scaled.
- [x] **P2.7** (S) `stdDisplay_SaveScreen`, plus `--screenshot-at <frame>` so images can be captured by script. Done:
      `--screenshot-at <n>[,<n>...]` and `--screenshot-file`; `stdBmp_WriteVBuffer` now pads rows and sizes its header
      correctly. Checked: `display` test; a headless run writes BMPs that `sips` reads.
- [x] **P2.8** (S) Remaining `stdDisplay` entries: `CreateZBuffer`, memory queries, `SetDefaultResolution`, clipper
      functions as no-ops. The four `*BufferDC` functions and `FlipToGDISurface` stay null until P6.5. Done: memory
      queries report the Metal device's working set. `Refresh` also stays null (P2.19).

#### 2B. Metal rasterizer (`std3D_metal.c`, `render_metal.m`)

- [x] **P2.9** (M) Skeleton: device, command queue, depth texture, one command buffer per frame; `Startup`, `Open`,
      `Close`, `StartScene`, `EndScene`, `ClearZBuffer`; device and display-environment enumeration reporting one
      Metal device; the texture-format functions (BGRA with and without alpha, as the null backend reports now).
      Done: `std3D_metal.c` (engine side, C) over `render_metal.m` (GPU side), on a Metal context shared with the
      presenter. Scenes draw into a scene texture that starts from the back buffer and becomes the frame's picture.
      Checked: all tests pass; `boot-sdl` saves still match `boot`; the `present` test checks a scene's picture is
      what gets presented (`docs/PORTING.md`, "Renderer skeleton").
- [x] **P2.10** (M) Shaders: the vertex stage converts pre-transformed screen coordinates and 1/w to clip space; the
      fragment stage is texture × diffuse + specular, as Direct3D 6 fixed function did. Done: as retail configured
      Direct3D (`std3D_InitRenderState`): texture × diffuse with specular off (its alpha is the fog factor, P2.14),
      alpha test greater-than, source-alpha blending, depth less-or-equal, no culling, depth clamped; half-pixel
      centre shift; `J3DRender_Draw`. Checked: new `render` test, offscreen, read back (`docs/PORTING.md`, "Shaders").
- [x] **P2.11** (M) Geometry: a per-frame vertex and index buffer ring; `DrawRenderList`; `DrawLineStrip` and
      `DrawPointList`. Check: untextured, unlit level geometry is on screen with correct depth. Done: a per-scene
      arena instead of a ring (`BeginScene` waits for the GPU); screenshots copy the scene back (part of P2.17).
      Checked: a headless screenshot of `00_cyn` shows the canyon, lit and correctly occluded; `render` test overflows
      the arena.
- [x] **P2.12** (M) Textures: `AllocSystemTexture` (mip chain upload), `ClearSystemTexture`, the texture-cache
      functions (`AddToTextureCache`, `PurgeTextureCache`, `ResetTextureCache`, `UpdateFrameCount`),
      `GetValidDimensions`, `GetMipMapCount`, `FindClosestFormat`. Done: one Metal texture per material level chain,
      referenced by the retail cache bookkeeping; RGB textures opaque by swizzle; also `SetMipmapFilter` and the
      sampler cache. Checked: `00_cyn` textured in a headless screenshot; `render` test texture cases.
- [x] **P2.13** (M) Render states: map the `SetRenderState` bitmask to sampler states (bilinear or nearest, clamp or
      wrap per axis), blending, z-write and alpha reference; `SetMipmapFilter`; a pipeline cache keyed by blend mode.
      Done: retail's one blend mode needs no cache; `SetMipmapFilter` landed with P2.12. Checked: `render` test.
- [x] **P2.14** (S) Fog: `EnableFog` and `SetFog` as fragment uniforms using the engine's start and end depths.
      Done: Direct3D's linear table fog by eye depth; retail's density quirk kept. Checked: `render` test, `11_PYR`.
- [x] **P2.15** (S) `SetProjection` and viewport; drawable size against the engine's mode size on Retina displays.
      Done: high-density window, mouse positions mapped to back-buffer pixels; homogeneous clipping of faces behind
      the eye fixed (floors were missing in three levels). Checked: `render` and `present` tests, level screenshots.
- [x] **P2.16** (S) `SetWireframeRenderState`. Done: retail's state change and no texture.

#### 2C. Integration

- [x] **P2.17** (M) Frame order: trace how the retail frame interleaves the 3D scene, the HUD and back-buffer 2D
      (console, text, frozen frame under dialogs), record it in `docs/PORTING.md`, and composite in that order.
      Done: HUD, console and text all go through `std3D`; CPU access is a clear before the scene and reads after it
      (thumbnails, screenshots). The scene texture is the frame's picture until the CPU touches the back buffer,
      which first takes the scene back. Checked: `present` test (CPU fill after a scene).
- [x] **P2.18** (S) Temporary debug input (a handful of keys read straight from SDL) so a level can be walked before
      phase 3. Removed in P3.2. Done: `stdControl_debug.c` replaces the generated keyboard half of `stdControl`
      (`stdControl_keys_null.c`). Checked: holding the up arrow walks Indy forward (headless screenshots).
- [x] **P2.19** (S) Window behaviour: resize, fullscreen toggle, minimize and focus loss mapped to the activate
      messages so the game pauses as retail did. Done: resizable window, Alt+Return and Cmd+Ctrl+F through the
      game's display restart, fullscreen minimizes on focus loss, `stdDisplay_Refresh` restores it. Checked: toggle
      and pause by temporary scripted events (game time stops while unfocused).
- [x] **P2.20** (S) 4:3 pillarbox option; the HUD layout assumes 4:3 until the widescreen fixes in P8.9. Done: the
      `Pillarbox` setting, on by default; mouse positions follow. Checked: `present` test.
- [x] **P2.21** (S) Remove `rdModel3K_null.c` and the `rdModel3K_*` references (11 sites) now that nothing can enable
      the path. Done: the null file and its generator entry removed; the three calling files keep retail behind
      `#ifndef J3D_PORT`.
- [x] **P2.22** (M) Visual pass: script a first-frame screenshot of all 17 levels and review each for missing
      textures, wrong alpha, z-fighting and fog errors. Fix what it shows. Done: `tools/level_shots.sh`; all 17 levels
      reviewed at frames 1, 300 and 900. Fixed: floors behind the camera (P2.15), fog density (P2.14), and scenes now
      start from the previous frame unless the CPU touched the back buffer (stale areas, screenshots changing later
      frames). Checked: `present` test, byte-identical screenshots.
## Complete Phase 2 Code Audit

Done 2026-11-13; details in `docs/PORTING.md`, "Phase 2 code audit". Extra warnings, the static analyzer, every
level under ASan and UBSan (the renderer now reaches rdroid's clipping and model code), and a read-through of the
phase's code.

- [x] Warnings and static analysis: nothing in the new code; the reports in touched imported files are on paths the
      game does not run.
- [x] Sanitizers over all 17 levels found two retail faults, fixed: a dangling clip frustum once 128 sectors are
      visible (`03_SHS`) and a vector written past a 2D local (`17_PRU`). 900 frames of every level are clean.
- [x] Read-through: a scene taken back mid-scene (the save menu's thumbnail) now copies what was drawn so far.
- Left open, each with its phase: the 128-sector and animation-slot limits (phase 8 fix series); the intro movie's
  colour format (phase 5); keys and minimizing in a real window, by hand until phase 3.
### Phase 3: Input

Exit test: `00_cyn` is playable start to finish with keyboard and mouse, and again with a controller, using the
retail keysets.

- [x] **P3.1** (S) DIK tables in `platform/common/`: SDL scancode to `DIK_*`, and `DIK_*` to display name.
      Done: `host_keys.c`, keyed by USB HID usage (SDL's scancodes) so it needs no SDL; Windows' US key names, which
      also back `GetKeyNameTextA`. Checked: `keys` test.
- [x] **P3.2** (M) `stdControl_sdl.c`, keyboard: `Startup`, `Open`, `Close`, `Reset`, `ReadControls`, `FinishRead`,
      `ReadKey` with retail's pressed-this-frame semantics, `ReadKeyAsAxis`, `ControlsActive`, `SetActivation`,
      `ControlsIdle`. Done: the whole module, retail's file with DirectInput replaced; `stdControl_debug.c` (P2.18)
      removed. Checked: `input` test (a held key moves Indy).
- [x] **P3.3** (S) Key and character messages to the window proc for the console and typed input. Done: key messages
      carry the scan code and extended flag; `WM_CHAR` was already there (P2.2). Checked: `input` test (F5 quicksave
      through `WM_KEYDOWN`).
- [x] **P3.4** (M) Mouse: relative mode while controls are active, `ReadAxis`/`ReadAxisRaw`, buttons, `EnableMouse`,
      `ToggleMouse`, `ShowMouseCursor`, `ResetMousePos`, sensitivity scaled to what DirectInput deltas were. Done:
      motion in points scaled by `Mouse Scale` (percent); buttons and wheel as buffered events. Checked: `input` test
      (motion turns Indy); capture and release by hand.
- [x] **P3.5** (S) Axis bookkeeping: `RegisterAxis`, `EnableAxis`, `SetAxisFlags`, `TestAxisFlag`, `ReadAxisAsKey`,
      `ReadAxisAsKeyEx`. Done: retail's arithmetic; `ReadAxisAsKey`'s out-of-bounds index fixed. Checked: `input` test
      (walk at half deflection, run at full).
- [x] **P3.6** (M) Gamepads through `SDL_Gamepad`, hot-plug included, mapped into the engine's slots (8 devices, 6
      axes, 32 buttons, 4 POV hats); `GetNumJoysticks`, `GetMaxJoystickButtons`, `IsGamePad`, `DisableReadJoysticks`.
      Done: Xbox-style DirectInput layout, right stick X also on Rz; slots keep their number; a new slot is rebound
      (`stdControl_DevicesChanged`); `Joystick Control` on by default. Checked: `input` test (scripted controller);
      a real controller and its hot-plug by hand.
- [x] **P3.7** (S) Load the retail `KEYSETS/*.kfg` through the data root; key names come from our DIK name table
      instead of `GetKeyNameTextA`. Done: `SearchPathA`; two retail faults the first load exposed fixed (an `int`
      written as `size_t`, the scheme name overrunning its buffer). Eight of eleven load; three lack retail's enabled
      flag. Checked: `input` test (SideWinder key set).
- [x] **P3.8** (S) A default keyset for a standard dual-stick controller, written for the port. Done: "Dual-Stick
      Controller" (BUILDING.md, "Controls"), chosen when nothing is configured and slot 0 holds one. Checked: `input`
      test.
- [x] **P3.9** (S) Mac keyboard behaviour: Command-Q quits, Command-Tab and focus loss release the mouse, Option and
      Command map to the Alt and Windows DIK codes. Done: the DIK table; the window's focus releases the mouse at
      once. Checked: `keys` test; Cmd-Q and Cmd-Tab by hand.
- [x] **P3.10** (M) Input recording and replay by frame, for the regression and A/B work in phase 7. Done:
      `host_record.c`, `--record`, `--replay`, `--input-script`; the frame clock makes a played run reproducible.
      Checked: `input` test (byte-identical quicksave).
## Complete Phase 3 Code Audit

Done 2026-11-13; details in `docs/PORTING.md`, "Phase 3 code audit". Extra warnings and the static analyzer on the
phase's code, the newly reachable retail code (inventory menu, key sets, bindings) under ASan and UBSan with scripted
input, and a read-through.

- [x] Warnings and static analysis: nothing in the new code.
- [x] Newly reachable retail code: the inventory menu read the inventory type table at index -1 for its category
      entries (18 reads, fixed), and `JonesHud_MenuClose`'s unset `moveRate` (fixed); the key set parser's two faults
      (P3.7).
- [x] Read-through: snapshot initialisation, slot bookkeeping, mouse release on every path, replay isolation.
- [x] Behaviour: `boot` and `boot-sdl` write the same autosave as at the phase 2 commit; every test passes in the
      apple, null and sanitizer trees.
- Not yet run: the exit test itself, `00_cyn` played through with keyboard and mouse and again with a controller,
  which needs a person; scripted input covers the engine side of it.
- Left open, each with its phase: by-hand checks with a real window and controller (relative mouse, Cmd-Tab, hot-plug);
  the inventory item spin bound OpenJones3D adds (phase 8 fix series); the controls dialog (phase 6).
### Phase 4: Audio

Exit test: music, ambience, effects, 3D positioning and dialogue with subtitles work in the first three levels, and
survive a save and load.

- [x] **P4.1** (S) Vendor `miniaudio.h` into `third_party/` and link it; open and close a device.
      Done: 0.11.25, hashes in `third_party/miniaudio/README.port.md`; device only, Core Audio only, 48 kHz float;
      `platform/apple/audio_device_ma.c` behind `host_audio.h`, none in the null build. Checked: windowed runs.
- [x] **P4.2** (S) Make the buffer handle opaque: `LPDIRECTSOUNDBUFFER` in `sound/types.h` becomes `SoundVoice*`.
      Done: and `LPDIRECTSOUND` becomes `SoundDevice*`.
- [x] **P4.3** (S) `SoundDriver_ma.c`, device level: `Open`, `Close`, `SetOuputFormat`, `SetMaxVolume`, the time and
      sleep functions, and the DirectSound object accessors as no-ops.
      Done: as `platform/common/SoundDriver_mix.c`, shared by every build; opens with or without a device.
- [x] **P4.4** (M) Voices: `CreateAndPlay`, `Play`, `Stop`, `Release`, `GetStatusAndCaps`, `GetCurrentPosition`,
      looping, and the buffer-data callback that `Sound.c` uses to fill and refill buffers.
      Done: state on the engine clock, rendering on the device's (decision 19); PCM copied at creation, so the
      refill callback (DirectSound's lost buffers) is never needed. Checked: `sounddriver` test.
- [x] **P4.5** (S) Volume, pan and frequency. Port the four pure conversion functions (`GetDecibelVolume`,
      `GetIntPan`, `GetVolumeFromDecibels`, `GetPanFromTable`) from retail `Driver.c` unchanged.
      Done: DirectSound's decibel volume and pan as gains, its frequency limits. Checked: `sounddriver` (retail's
      values, the mix's gains).
- [x] **P4.6** (M) 3D: listener position and orientation, `SetPosAndVelocity`, `Update3DSound`, `SetGlobals`
      (distance, rolloff, doppler), `ListenerCommitDeferred` applying a frame's changes together, `Has3DHW` true.
      Done differently: `Has3DHW` is false and sounds are placed by the game's own mix, retail's path without 3D
      hardware (decision 19); `Update3DSound` is retail's software half, the 3D-buffer calls keep their values only.
      Its uninitialised mix values fixed.
- [x] **P4.7** (S) Startup path: `Sound` opens without the `JONES_STR_SOUNDERROR` message and
      `Sound_Set3DGlobals: module is not open` no longer appears in the log.
      Checked: `boot` log; `J3D_NO_AUDIO` and `--headless` open no device and change nothing else.
- [x] **P4.8** (M) `AudioLib.c`: verify the C translation of the asm sample loop against decoded reference output
      and fix the unsequenced write at line 1703.
      Done: with no Windows build, the encoder is the oracle; twelve signals decode bit for bit against an independent
      decoder and end in the encoder's state (`sounddriver`). The write fixed, `-Wno-error=unsequenced` removed;
      38 unaligned accesses and the lip-sync overrun fixed (ASan, UBSan).
- [x] **P4.9** (M) Listening pass on levels `00` to `02`: looping ambience, attenuation and panning, voice lines with
      subtitles and lip sync (lip-sync data is regenerated on load), pause on focus loss, handles after a restore.
      `Sound_Restore` adds the saved pause time where `Sound_Save` subtracted it (found by P1.20): decide whether
      it matters once sound is audible.
      Done by measurement: `J3D_AUDIO_DUMP` mixes of the three levels (sound throughout, panned, lip sync generated);
      the pause time fixed (it broke the round trip, nothing audible); restored channel handles shift by 1234 in
      retail, kept (phase 8). Not done: listening by a person.
- [x] **P4.10** (S) Output device changes while running (headphones in and out).
      Done: miniaudio follows the default device and keeps the stream's format. Not done: the check by hand.
- [x] **P4.11** (S) Delete the DirectSound part of `win32_shim_dx.h`.
      Done: and the `dsound.h` and `Mmreg.h` stand-ins.
## Complete Phase 4 Code Audit
### Phase 5: Movies

Exit test: the five intro movies and every cutscene movie in the GOBs play with audio in sync and can be skipped; a
headless decode test passes for all of them.

- [ ] **P5.1** (M) Packing audit: INSANE was built with 1-byte struct packing. List the structs whose layout depends
      on it, pack those explicitly and assert their sizes.
- [ ] **P5.2** (S) `Stdfile.c` and `Stdio.c` over POSIX, going through the host path conversion.
- [ ] **P5.3** (M) `Timer.c` over a pthread with a mutex, keeping the callback contract the player expects.
- [ ] **P5.4** (S) `MemCopy.asm`, `MemFill.asm`, `BlitAsm.asm` to C (193 lines).
- [ ] **P5.5** (M) `DecodeAsm.asm` to C (519 lines): the two LUT decoders and six 555/565/RGBA conversions.
- [ ] **P5.6** (S) `MixerAsm.asm` to C (306 lines).
- [ ] **P5.7** (L) `Bloc16Asm.asm` to C (797 lines): the Blocky16 block decoder, cross-checked against ScummVM's.
      Its frame offsets are already `ptrdiff_t` (P1.27): the three frames are separate allocations.
- [ ] **P5.8** (M) `tests/moviecheck.c`: decode every frame of every movie with no display and checksum the frames.
      Compare with checksums from the Windows build under DREAMM when those can be captured.
- [ ] **P5.9** (M) `Dsnd.c`: a streaming source on the phase 4 audio device, fed by INSANE's mixer.
- [ ] **P5.10** (S) `JonesMain_IntroMovieBlt32` into the back buffer, scaled to the window. The 555 and 565 variants
      are not needed.
- [ ] **P5.11** (S) Skip, volume (`SmushPlay_SetGlobalVolume`) and subtitle hooks.
- [ ] **P5.12** (S) List every movie in the GOBs with `gobtool.py` and play each from its in-game trigger.
## Complete Phase 5 Code Audit
### Phase 6: GUI dialogs

Exit test: every dialog reachable in a playthrough works; the game can be configured, saved and loaded in-app.

The game is playable without dialogs from phase 3 on, so this phase can overlap phases 4 and 5.

#### 6A. Foundation (`platform/common/j3dgui/`)

- [ ] **P6.1** (S) Classify the 254 stubs in `win32_null.c`: dialog API to implement, GDI to replace, dead. The
      result is the shim's scope.
- [ ] **P6.2** (S) `tools/rc2table.py`: generate C tables from `Indy3D.rc` (26 dialog templates, control IDs, classes,
      positions in dialog units, strings).
- [ ] **P6.3** (M) Window objects: an `HWND` tree with IDs, text, rectangle, enabled, visible and focus state;
      `GetDlgItem`, `SetDlgItemTextA`, `EnableWindow`, `ShowWindow`, `SetFocus`, `MoveWindow`, `GetWindowRect`,
      `InvalidateRect`.
- [ ] **P6.4** (M) Modal loop: `DialogBoxParamA` and `EndDialog`, `WM_INITDIALOG`, `WM_COMMAND`, `WM_NOTIFY` and
      `WM_CLOSE` dispatch, a nested loop that pumps SDL events and presents, as `DialogBoxParam` nested a message loop.
- [ ] **P6.5** (M) Renderer: draw the dialog over the captured game frame in the back buffer with `rdFont`, replacing
      `JonesDialog.c`'s GDI compositing (52 device-context and blit call sites in `Jones3D/`).
- [ ] **P6.6** (M) Controls 1: static text, push button, check box, radio button, group box, with keyboard (Tab,
      Return, Escape), mouse and controller navigation.
- [ ] **P6.7** (M) Controls 2: list box and combo box with item data (`LB_*`, `CB_*`).
- [ ] **P6.8** (M) Controls 3: trackbar (`TBM_*`, the most used message family), the list view, the edit control and
      the scroll bar.
- [ ] **P6.9** (S) Subclassing: `JonesDialog_SubclassDialogProc` and the stored `WNDPROC` chain in `jonesConfig.c`.
- [ ] **P6.10** (M) Icons and bitmaps: `Indy3D.rc` names 43 image files that exist only inside the retail executable.
      Read them from the player's `INDY3D.EXE` resource section at run time, with a plain fallback when it is absent.

#### 6B. Dialogs, in order of how early the game needs them

- [ ] **P6.11** (S) Message box (template 121). The game already calls it at startup today.
- [ ] **P6.12** (S) Confirmations: exit game (211), save overwrite and save messages (163, 214), purchase message
      (212), insert CD (167, which the data root should make unreachable).
- [ ] **P6.13** (M) Developer dialog (101, 106): level list and options; gates `Start Mode` 2.
- [ ] **P6.14** (S) Game play options (112).
- [ ] **P6.15** (S) Sound settings (113).
- [ ] **P6.16** (M) Display settings (114) and advanced display settings (148), fed by the P2.3 mode list.
- [ ] **P6.17** (L) Save game (154) and load game (159): replace `GetSaveFileName`/`GetOpenFileName` with an in-game
      file list, and the GDI thumbnail in `sithGamesave` with a VBuffer.
- [ ] **P6.18** (M) Game over (150), statistics (164), level completed (233).
- [ ] **P6.19** (L) Indy's trading post (190): item list, cart, icons, purchase.
- [ ] **P6.20** (L) Control options (111) with create scheme (116) and edit scheme (115).
- [ ] **P6.21** (M) Assign key (117) and reassign key (120), which read raw input from `stdControl`.
- [ ] **P6.22** (S) Confirm templates 104, 105 and 118 are unreferenced and leave them out.
- [ ] **P6.23** (S) Restore retail's `Start Mode`, `Debug Mode` and `Verbosity` defaults, which were changed in
      phase 1 only because no GUI existed.
- [ ] **P6.24** (M) `jonesConfig.c` clean-up left from earlier phases: its path handling (128-byte file-dialog
      buffers), its 58 narrowing warnings (then drop its `-Wno-error=shorten-64-to-32` in `CMakeLists.txt`) and its
      four uninitialised locals (`docs/PORTING.md`, "Known open items"). Its fall-off-the-end functions were fixed in
      P1.22.
- [ ] **P6.25** (M) Layout pass: every dialog compared with a reference screenshot from the Windows build, at 1× and
      2× scale.
## Complete Phase 6 Code Audit
### Phase 7: Full-game verification and 64-bit closure

Exit test: all 17 levels completed on the port with no assert, crash or blocker, and no Win32 type outside
`platform/`.

- [ ] **P7.1** (S) One command that configures, builds, runs `ctest` and `layout_audit.py`; run it before every
      commit.
- [ ] **P7.2** (M) Savegame round trip (P1.20) for every level.
- [ ] **P7.3** (M) Build with `J3D_DEBUG_ASSERTS=ON`, run the tests and a session per level, fix what asserts.
- [ ] **P7.4** (L) Playthrough, recorded in the table below.
- [ ] **P7.5** (M) A/B against the Windows build in DREAMM for the physics-sensitive places: long jumps, whip swings,
      raft and jeep handling, the Marduk statue push that 1.2 fixed (risk 7.1). Include the end of every item push
      and pull: retail's final step used an unset direction, which the port sets to the push direction (audit).
- [ ] **P7.6** (M) Delete `win32_shim_dx.h` (489 lines) once phases 2 to 5 have removed its last users.
- [ ] **P7.7** (M) Delete `win32_shim_gui.h` (1,538 lines) and `win32_null.c` once phase 6 is complete.
- [ ] **P7.8** (M) Delete `win32_shim.h` (490 lines): no `HWND`, `HDC` or `DWORD` left outside `platform/`.
- [ ] **P7.9** (M) Performance and memory: frame times on a base M1, leak check over a level change, a long session.
- [ ] **P7.10** (S) `docs/PORTING.md` covers every phase.

Playthrough record for P7.4. "Loads" is the `levelload` test.

| Level | File | Loads | Renders | Completed |
| --- | --- | --- | --- | --- |
| Canyonlands | `00_cyn` | ✅ | ⬜ | ⬜ |
| Babylon | `01_bab` | ✅ | ⬜ | ⬜ |
| Tian Shan River | `02_riv` | ✅ | ⬜ | ⬜ |
| Shambala Sanctuary | `03_shs` | ✅ | ⬜ | ⬜ |
| Palawan Lagoon | `05_lag` | ✅ | ⬜ | ⬜ |
| Palawan Volcano | `06_vol` | ✅ | ⬜ | ⬜ |
| Palawan Temple | `07_tem` | ✅ | ⬜ | ⬜ |
| Teotihuacan | `08_teo` | ✅ | ⬜ | ⬜ |
| Olmec Valley | `09_olv` | ✅ | ⬜ | ⬜ |
| V.I. Pudovkin | `10_sea` | ✅ | ⬜ | ⬜ |
| Meroë | `11_pyr` | ✅ | ⬜ | ⬜ |
| King Sol's Mines | `12_sol` | ✅ | ⬜ | ⬜ |
| Nub's Tomb | `13_nub` | ✅ | ⬜ | ⬜ |
| Infernal Machine | `14_inf` | ✅ | ⬜ | ⬜ |
| Aetherium | `15_aet` | ✅ | ⬜ | ⬜ |
| Jeep Trek | `16_jep` | ✅ | ⬜ | ⬜ |
| Return to Peru (bonus) | `17_pru` | ✅ | ⬜ | ⬜ |
## Complete Phase 7 Code Audit
### Phase 8: Mac product polish

Exit test: a notarized `.app` that a player with a retail copy can install and finish the game with, on a machine that
has no developer tools.

- [ ] **P8.1** (S) Choose the product name and bundle identifier, avoiding LucasArts and Disney marks (the working
      name is J3DPort).
- [ ] **P8.2** (M) `.app` bundle target: `Info.plist`, an original icon, high-resolution capable, games category, Game
      Mode eligibility.
- [ ] **P8.3** (M) Ship dependencies inside the bundle (SDL3 static or embedded); nothing loaded from Homebrew paths.
- [ ] **P8.4** (M) First-run data locator: look in the Steam, GOG and DREAMM install locations, otherwise an open
      panel; store the result as `Data Root`.
- [ ] **P8.5** (S) Validate the chosen folder: GOB names and sizes, and the `INDY3D.EXE` hash against
      `docs/VERSIONS.md`. Decide and state which data versions are supported (only 1.2 is tested so far).
- [ ] **P8.6** (S) Command line: `--level`, `--windowed` (`--data` exists).
- [ ] **P8.7** (S) Display preferences: window or fullscreen, vsync, frame limiter.
- [ ] **P8.8** (M) Triage OpenJones3D's `CHANGELOG.md` (~80 entries) into crash fixes, raised limits and quality of
      life, each with its upstream commit.
- [ ] **P8.9** (L) Apply the series behind a build option that defaults on, in groups, each retested: crash and
      memory fixes; COG stack and sound-handle fixes; raised limits (128 sounds, mesh and sector limits); widescreen
      and HUD scaling.
- [ ] **P8.10** (S) Keyboard and controller glyphs in the control dialogs.
- [ ] **P8.11** (S) Crash handling: write the engine log beside the crash report and tell the player where it is.
- [ ] **P8.12** (S) README and in-app notices: a retail copy is required, AGPL source offer, credits.
- [ ] **P8.13** (M) Hardened Runtime, code signing, notarization, and a disk image.
- [ ] **P8.14** (S) Clean-machine test on macOS 13 (the deployment target) and on the current release.

### Phase 9: Stretch (not counted)

Universal binary; higher-resolution textures from loose files; MSAA and anisotropic filtering; an SDL_GPU backend for
Linux and Windows; offering the portable platform layer to OpenJones3D.

## 6. Repository layout

What exists today, and what each phase adds.

```
j3dport/
  CMakeLists.txt        exists
  LICENSE, NOTICE       exists
  Jones3D/              imported; Win32 calls resolved by the shim until phase 7
  Libs/{sith,rdroid,std,sound,smush,zlib,j3dcore,wkernel,w32util,intel}   imported
  platform/apple/       main.c, shim/, host_apple.m, wkernel_sdl.c, stdDisplay_sdl.c, display_metal.m,
                        std3D_metal.c, render_metal.m, stdControl_sdl.c, audio_device_ma.c
                        + bundle and locator (8)
  platform/common/      crt_posix.c, host_path.h, host_clock.h, host_dirs.c, wuRegistry_file.c,
                        stdDisplay_vbuffer.c, stdDisplay_common.c, host_screenshot.c,
                        null/ (with stdDisplay_headless.c)
                        SoundDriver_mix.c, host_audio.h     + insane_os/ (5), j3dgui/ (6)
  tools/                gen_null_platform.py, layout_audit.py, gobtool.py     + rc2table.py (6)
  tests/                harness.c, levelload.c, cogcheck.c, saveload.c, display.c, present.c, render.c,
                        keys.c, input.sh, sounddriver.c     + moviecheck.c (5)
  third_party/          miniaudio/ (4)
  docs/                 PORT-PLAN.md, PORTING.md, and the decomp's reference documents
```

The decomp's documents in `docs/` stay as the behavioural reference: file formats, versions and the known 1.2
changes. `docs/BUILDING.md` is the port's build guide; the decomp's is `docs/DECOMP-BUILDING.md`.

## 7. Risks and how they are handled

### 7.1 Floating-point behaviour

The original ran on x87 with 64-bit internal precision and explicit rounding-mode switches. arm64 uses true 32-bit
floats and fused multiply-add by default. Physics, whip swings and COG arithmetic may diverge subtly. In place:
`-ffp-contract=off`, the `NONMATCHING_MATH` C bodies, the engine's own trigonometry tables. Still to do: the A/B of
known-sensitive puzzles (P7.5), which needs input replay (P3.10) to be repeatable. OpenJKDF2 shipped the same engine
family on arm64 Macs this way without gameplay regressions.

### 7.2 On-disk struct layouts on 64-bit

Largely retired. The layout audit reports no differing on-disk type, every fixed record asserts its retail size, and
all 17 levels load with plausible counts. The run-time side is closed too: every narrowing conversion was
triaged and is now a compile error (P1.25 to P1.29; four were real 64-bit bugs), the sanitizer build is clean on the
tested paths (P1.21), and savegames, which only the port reads back, round-trip (P1.20; all levels in P7.2). What is
left is the code no test reaches yet (rendering, sound, movies, dialogs), which the later phases run under the
sanitizer build. Draft 1 expected `SithCogValue` to break level loading;
it does not, because `.cnd` files store cog symbol values as text.

### 7.3 Dialog system scope

`jonesConfig.c` is 10k lines and the shim must honour exact message semantics. The scope is now measurable: 254
stubbed Win32 functions, 26 templates, and a known control set (section 2.1). Two things were not in draft 1: five
list-view controls, and 43 icons and bitmaps that are game assets inside the retail executable and so cannot be
shipped (P6.10). The game is playable without dialogs from phase 3, so the schedule risk stays inside phase 6.

### 7.4 Blocky16 and INSANE correctness

Hand-written assembler with no debug build to read, and structures that assume 1-byte packing. Mitigation: the packing
audit first (P5.1), ScummVM's codec as reference (P5.7), and a headless frame-checksum test (P5.8).

### 7.5 Licensing and trademarks

AGPLv3 code, no assets, a product name that avoids LucasArts and Disney marks (P8.1), and a README stating a retail
copy is required. ScummVM-derived code must keep its GPL notices; GPLv2+ is compatible with AGPLv3.

### 7.6 Tooling

SDL3 and miniaudio are stable; Metal and Apple clang are first-party. No deprecated framework is used.

### 7.7 Retail bugs that only worked by accident

New since draft 1. Phase 1 found retail code that depended on the 1999 compiler or hardware: globals assumed adjacent
in memory, a read one byte before a buffer, a material loader that only worked when textures stayed 16-bit, 20
functions whose return value was whatever the register held. More will surface once the game loop runs. Mitigation:
the sanitizer build (P1.21), the debug-assert build (P7.3), and logging each one in `docs/PORTING.md` so it is not
mistaken for a port regression. The phase 1 audit found eleven more such reads of uninitialised locals (an item
push's final direction among them) and gave each a defined value (decision 18).

## 8. Milestones

Draft 1's estimates assumed one engineer working by hand and are kept for comparison only. Phase 0 and most of phase 1
took two calendar days, so they are not a forecast of the remaining pace; the step sizes in section 5 are the better
guide.

| # | Milestone | Draft 1 estimate | State on 2026-11-12 |
| --- | --- | --- | --- |
| 0 | Tree links on arm64 with a null platform | 1 week | Reached 2026-11-12 (`2d8bbed`) |
| 1 | Headless boot, all levels load, 64-bit audit closed | 2 to 3 weeks | Reached 2026-11-12: levels load, scripts compile, the game loop runs and saves round-trip headless, sanitizer-clean, all warnings errors; merged into `main` |
| 2 | Metal renderer, first level on screen | 3 to 4 weeks | Complete: every level renders textured, lit and fogged in a window or fullscreen; the phase audit is done |
| 3 | Playable with keyboard, mouse and gamepad | 1 to 2 weeks | Code complete and audited 2026-11-13; the playthrough by hand is open |
| 4 | Audio complete | 2 weeks | Code complete and audited 2026-11-13; the listening pass by hand is open |
| 5 | Movies complete | 2 to 3 weeks | Not started |
| 6 | All dialogs, save and load in-app | 4 to 6 weeks | Not started |
| 7 | Full playthrough verified | 2 to 3 weeks | Not started |
| 8 | Signed `.app`, data locator, fixes | 2 to 3 weeks | Not started |

Ordering: phase 2 needs 1C (P1.17, P1.18) but not 1D. Phases 3, 4 and 5 are independent of each other once phase 2
lands. Phase 6 needs phases 2 and 3. Phase 7 needs everything before it.

## 9. Immediate next steps

1. **By hand, before phase 7 relies on them:** the phase 3 playthrough of `00_cyn` (keyboard and mouse, then a
   controller) and the phase 4 listening pass on the first three levels through a save and load, with headphones
   plugged in and out once. `J3D_AUDIO_DUMP` (BUILDING.md) records what was heard, for comparing later builds.
2. **Phase 5, movies:** INSANE's timer, file I/O and audio sink (the sink can feed the phase 4 mixer as one more
   voice), then the six assembler files as C, `Blocky16` against ScummVM's.
3. Keep the sanitizer build in the pre-commit routine (`docs/BUILDING.md`); every phase from 2 on adds code paths the
   current tests do not reach.

To reproduce the measurements at the top of this file:

```
cmake -S . -B build -G Ninja -DJ3D_TEST_DATA=<folder holding RESOURCE/>
cmake --build build
ctest --test-dir build --output-on-failure
python3 tools/layout_audit.py
cmake -S . -B build-asan -G Ninja -DJ3D_SANITIZE=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DJ3D_TEST_DATA=<same folder>
cmake --build build-asan && ctest --test-dir build-asan --output-on-failure
```

When a step is finished, tick it here and update its row
in the table at the top of this file.
