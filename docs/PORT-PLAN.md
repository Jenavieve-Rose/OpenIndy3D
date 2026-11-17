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
| 4 | Audio | 11 of 11 | Code audit done; listened to by hand (both sound paths); first three levels and save/load by ear open |
| 5 | Movies (INSANE) | 12 of 12 | Code audit done; watched and listened to by hand open |
| 6 | Launcher and Options (SwiftUI), in-game menus (SwiftUI): save and load, store, controls | 41 of 41 | Complete 2026-11-16 (decisions 20 and 21), audited, merged into `main`; menus by hand open |
| 7 | Full-game verification, shim reduced (decision 22) | 8 of 10 | Code complete and audited 2026-11-17; playthrough (P7.4) and DREAMM A/B (P7.5) by hand open |
| 8 | Signed `.app`, data locator, fixes and polish | 12 of 14 | Audited 2026-11-17; notarization with a Developer ID (P8.13) and the macOS 13 test (P8.14) open |
| **All** | | **158 of 162** | |

Step counts are not effort: the open steps in phases 2 and 6 are much larger than the closed ones in phases 0 and 1.
Open steps carry a size, **S** (hours), **M** (a day or two) or **L** (several days), for one engineer.

### What works today (measured 2026-11-13 on the working tree)

| Check | Result |
| --- | --- |
| `cmake --build` from clean, arm64, Apple clang | Builds and links with `-Werror`, exit 0. The only warnings are, in the default `J3D_PLATFORM=apple` tree, the same linker warning about Homebrew's SDL3 deployment target for each of the three programs that link SDL3 (P8.3) |
| `ctest` | `display`, `keys`, `sounddriver` and `insaneasm` pass (no game data needed), and in the apple build `render`, `present` (needs a window server) and `audio3d` (needs an audio output device). With `-DJ3D_TEST_DATA`: `levelload`, `cogcheck`, `saveload`, `saveload-3d`, `moviecheck` and `boot` pass, and `boot-sdl` and `input` in the apple build |
| The same under `-DJ3D_SANITIZE=ON` (ASan and UBSan) | All pass with no report; so does a 3,000 + 3,000 frame `saveload` run |
| `moviecheck <data>` | All five intro movies decode headlessly in about 4 s: every picture shown, all 2,240 Blocky16 codec-2 frames equal to a port of ScummVM's decoder, audio mixed and not silent, checksums as pinned |
| `levelload <data>` | Static world and all 17 levels load, 0 failed, section counts plausible |
| `cogcheck <data>` | 1,132 scripts compiled, 0 failed |
| `python3 tools/layout_audit.py` | `0 retail on-disk type(s) differ` |
| `jones3d --data <data>` | Apple build: opens the game window (fullscreen or windowed, at the mode the settings name), plays the intro movie with its sound (Return, Escape or a click skips it), and runs the game in it, rendered through Metal at the display's refresh, played with keyboard, mouse and controllers, with sound through the default output device. Null build: stops at the display enumeration (no display backend) |
| `jones3d ... --input-script` / `--record` / `--replay` | Scripted keyboard, mouse and controller input reaches the game; a recorded run replays to a byte-identical quicksave (`input` test) |
| `jones3d --data <data> --headless --frames 600` | Loads `00_cyn`, writes the level-start autosave, runs 600 frames (20 s of game time) of the opening cutscene and shuts down cleanly, exit 0, in under a second. Two runs print the same log and write byte-identical saves |
| `saveload <data>` | After 300 frames, a save restored into a fresh world matches the world that was saved (236 things, positions bit for bit, COG globals, game time), and a save of the restored game matches the first; the game then runs 300 more frames |
| Long paths | `boot` and `saveload` pass with a 257-character `Install Path`; `boot`, `levelload` and `cogcheck` with a 229-character data root |

Every level renders in a window or fullscreen through Metal: textured, lit, fogged, with the HUD, at the display's
refresh rate, pillarboxed to 4:3 by default, and plays with the keyboard, the mouse and controllers, using the retail
key sets or the port's controller default (phase 3), with music, ambience, effects and voices placed by the game's
own camera-relative mix, or with "Sound 3D" on in 3D with HRTF and doppler (phase 4). The intro movie plays with its
sound (phase 5). There is no launcher yet and the dialogs do not appear on `main` (phase 6); the in-game inventory menu works. `tools/level_shots.sh` screenshots
every level by script.

All of phase 1 is on `main` (at `3c3605a`): sixteen commits made on the branch `phase1-headless`, seven for P1.8 to
P1.18 (`28e2120` to `b4b56e7`) and nine for P1.19 to P1.30, the audit and the docs (`f0d0935` to `3c3605a`), merged
by fast-forward on 2026-11-12. Each builds and passes its tests; before `5ba51d2` (P1.30) the `boot` test needs a
build tree with a short path.

Phases 2 and 3 are on `main` (at `b0979f2`), merged by fast-forward on 2026-11-13: fourteen commits from the branch
`phase2-display`, thirteen for P2.1 to P2.22 (`b872038` to `ec42ba4`) and the audit (`22a2f9b`), then two from
`phase3-input`, P3.1 to P3.10 (`250a7a1`) and the audit (`b0979f2`). At `b0979f2` everything builds and every test
passes in the apple, null and sanitizer trees; the commits before it were not each rebuilt for the merge.

Phase 4 is on `main` (at `e9b6d42`), merged by fast-forward on 2026-11-13: two commits from the branch `phase4-audio`,
P4.1 to P4.11 (`70558f4`) and the audit (`e9b6d42`, documents only). At `70558f4` everything builds and every test
passes in the apple (11 tests), null (7) and sanitizer (11) trees.

3D sound (decision 19) followed on the branch `audio-3d`: one commit (`9c739bd`), merged into `main` by fast-forward on
2026-11-13. At `9c739bd` everything builds and every test passes in the apple (13 tests), null (8) and sanitizer (13)
trees.

Phase 5 is on `main` (at `cc734c9`), merged by fast-forward on 2026-11-13: two commits from the branch
`phase5-movies`, P5.1 to P5.12 (`2740902`) and the audit (`cc734c9`). At `cc734c9` everything builds and every test
passes in the apple (15 tests), null (10) and sanitizer (15) trees.

Phase 7 is on `main` (at `aab8199`), merged by fast-forward on 2026-11-17: five commits from the branch
`phase7-verification`, P7.1 to P7.3 (`e829bf0`), the shim (`732dc4d`), P7.9 (`474f967`), the audit (`f1d4530`) and the
documents (`aab8199`). At `f1d4530` everything builds and every test passes in the apple (35 tests), null (29),
sanitizer (35) and debug-asserts (35) trees (`tools/check.sh --asserts`); the commits before it were not each rebuilt.

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
| The port | `j3dport/` | Git repository, hard fork of the decomp at upstream `319a6fc2`; phases 0 to 4 are on `main` | **Where all work happens.** |
| Byte-exact decompilation | `infernal-machine-decomp-public-main/` | 100% source for `Indy3D.exe` v1.2 and v1.0, ~188k lines of C, plus zlib 1.0.9 and the INSANE movie player | Pristine upstream copy, for diffing and cherry-picks. Its docs are imported into `j3dport/docs/`. |
| OpenJones3D v0.3.1 | `OpenJones3D-main/` | Partial reimplementation (87%) that runs by injecting a DLL into the retail exe | Not a base. Source of ~80 catalogued fixes (`CHANGELOG.md`), applied in phase 8. |
| Retail game data | `Indy3D/` | `RESOURCE/` with `CD1.GOB`, `CD2.GOB`, `JONES3D.GOB`, five `.snm` intro movies, 51 patched `.cog` scripts in `Cog/`; `KEYSETS/*.kfg` | Complete v1.2 install (`INDY3D.EXE` = `55fb00c0…`). The test data root: `-DJ3D_TEST_DATA=…/Indy3D`. |
| OpenAL Soft | `openal-soft-master/` | Source tree, not referenced by the build | Fallback audio backend only (decision 7). |
| SDL3 | Homebrew, 3.4.20 | Installed on the development machine, not yet referenced by the build | Window, input, gamepads from phase 2. |

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
| `Libs/sound` | 6,713 | `Driver.c` is DirectSound; `Sound.c` and `AudioLib.c` are portable apart from the buffer handle type and one asm loop | Phase 4: `Driver.c` replaced by `platform/common/SoundDriver_mix.c` over an AVAudioEngine device; the handles are opaque; the asm loop's C translation verified (P4.8). Save and restore run with sound open. |
| `Libs/smush` (INSANE) | 10,267 | 6 MASM files (1,815 lines), `Dsnd.c`, `Timer.c`, `Stdfile.c`, `Stdio.c` | OS modules register nothing; the 14 asm entry points abort if reached. No movies. File handles and Blocky16 frame offsets made 64-bit clean in phase 1. |
| `Libs/wkernel`, `Libs/w32util` | 692 | 100% | `wkernel` is a hand-written null (message queue, quit). `wuRegistry` is done (`settings.ini`). |
| `Jones3D/` (game shell) | 23,318 | `jonesConfig.c` (9,955 lines of dialog procedures), `JonesDialog.c`, `JonesDevDialog.c`, `Indy3D.rc` (26 dialog templates) | Its dialogs are SwiftUI menus (decision 21) or, with no menu, its own dialog procedures over j3dgui (phase 6). Builds with `-Werror` since P6.24; its Win32 names are the port's emulation (decision 22). |
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
  must either implement or prove dead. (Done by phase 7: the file is deleted, decision 22.)
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
7. **Audio: AVAudioEngine** (revised in phase 4). Draft 1 chose miniaudio (single-file C, Core Audio backend,
   built-in spatializer), with OpenAL Soft as the alternative. miniaudio was the output device for phase 4 itself;
   when 3D sound was added it gave way to AVAudioEngine, whose AVAudioEnvironmentNode places voices with Apple's HRTF
   rendering and supports head tracking (decision 19). The mixing is the port's own; OpenAL Soft is not needed.
8. **Movies: port INSANE, do not swap in ScummVM.** The six assembler files become C. ScummVM's `Blocky16` codec
   (GPLv2+) is the reference for the hardest one.
9. **GUI: emulate the slice of the Win32 dialog API that `jonesConfig.c` uses**, backed by our own widgets, so ~10k
   lines of dialog logic stay intact. Narrowed by decision 20: this covers the dialogs the game raises while it
   runs, not the launcher, and not the Windows look.
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
19. **Sound: both of retail's paths, kept on the engine clock** (phase 4). With "Sound 3D" off (retail's default) every
    sound is placed by the game's own camera-relative mix. With it on, mono positioned sounds are 3D voices, as
    DirectSound 3D buffers were: the driver applies DirectSound 3D's distance model and doppler (from each sound's
    velocity and the camera's, which the port derives since the game passes none) and AVAudioEnvironmentNode places
    them around the camera with HRTF, oriented as the camera is (retail's listener had its top flipped to suit DirectSound's left-handed frame). The
    driver reports 3D hardware in every build. Everything the sound module can observe of a voice is computed from the
    engine clock, never from the device, so headless, recorded and replayed runs keep byte-identical saves and the
    null build runs the same driver silently (docs/PORTING.md, "The sound driver" and "3D sound").

Decision made while replanning phase 6 (2026-11-14):

20. **Two interfaces: a SwiftUI launcher, and the game's dialogs redrawn.** The launcher and its Options window are
    SwiftUI, in windows of their own, modelled on the Steam release's launcher; they change settings only through
    `settings.ini` and hold no game logic. The dialogs the game raises while it runs keep `jonesConfig.c`'s logic
    over the Win32 emulation (decision 9) but are drawn in the launcher's look, with the system font, instead of
    Windows 98's. Rewriting those dialogs in SwiftUI was weighed and set aside: it would mean rewriting the logic of
    each one, not only its drawing. Original art (the launch picture, the dialog icons) is read from the player's
    copy at run time and never shipped. (The second half, the dialogs redrawn over the emulation, was replaced two
    days later by decision 21.)

Decision made while doing phase 6 (2026-11-16):

21. **The in-game menus are SwiftUI too.** Asked for once the first SwiftUI menus were seen over the game: the menus
    the game raises while it runs (messages, exit, save and load, the options, game over, statistics, the store, the
    controls) are SwiftUI views laid over the game window as a modal session, in the launcher's look
    (`platform/apple/menu/`). Each `jonesConfig.c` entry point asks the platform's menu first
    (`platform/common/j3dmenu.h`); what the dialog did around its controls (its strings, its checks, the settings it
    applies and saves) stays in C, in a `#ifdef J3D_PORT` block in the entry point or in
    `platform/apple/j3dmenu_apple.c`, so the rules are still retail's. The Win32 dialogs over the emulation
    (decision 9, 6A) are kept, unrestyled, for runs that have no menu: headless runs, replays, the null platform, the
    tests, and `J3D_MENUS=classic`. The file dialogs are the exception: saving and loading have no Win32 fallback and
    cancel where there is no menu.

Decision made while doing phase 7 (2026-11-17):

22. **The Win32 and DirectX vocabulary that is used by design stays, declared by the port; nothing declared does
    nothing.** Phase 7 meant to delete the shim headers once the earlier phases had removed their users (P7.6 to
    P7.8). Some users stay by design: the device descriptions in `std/types.h`, the `DIK_*` scancodes the key sets
    store, sith's `DPID` addresses, the game window procedure over wkernel, and `jonesConfig.c`'s Win32 dialog
    procedures over j3dgui (decisions 9 and 21). Rewriting those would change thousands of lines of imported code
    against decision 2 for no change in behaviour. So: the DirectX names are the engine's own header
    (`Libs/std/Win95/stdDirectX.h`); every declaration nothing calls is deleted, with `win32_null.c`; every stub still
    reached is compiled out where Windows alone needed it, or made real; and the null generator refuses a declared
    function nobody implements. `win32_shim.h` and `win32_shim_gui.h` stay as the API of the port's Win32 emulation.

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
   ├─ apple/shim/                       the Win32 emulation's API            exists; trimmed in phase 7 (decision 22)
   ├─ apple/wkernel_sdl.c               event loop, window proc messages     exists
   ├─ apple/stdDisplay_sdl.c            display device, modes                exists
   ├─ apple/display_metal.m             back-buffer present over Metal       exists
   ├─ apple/std3D_metal.c, render_metal.m  Metal renderer                    exists
   ├─ apple/stdControl_sdl.c            keyboard to DIK, mouse, gamepads     exists
   ├─ common/SoundDriver_mix.c          voices on the engine clock, mixer    exists
   ├─ apple/audio_device_avf.m          AVAudioEngine output, 3D placement   exists
   ├─ common/insane_timer.c, insane_dsnd.c  INSANE's timer and audio sink    exists (asm→C beside the .asm files)
   ├─ common/j3dgui/                    Win32 dialog emulation and widgets   exists (6A); the fallback since decision 21
   ├─ common/j3dmenu.h, apple/menu/     SwiftUI in-game menus, j3dmenu_apple.c  exists (6C, 6D)
   ├─ apple/launcher/                   SwiftUI launcher and Options         phase 6 (6B)
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
      Fixed after phase 4: 16-bit textures were widened by shifting, so 1-bit-alpha textures failed the alpha test
      and vanished (Indy's revolver); they now expand to full range (docs/PORTING.md, "Textures"). Checked: `display`.
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
      Done: 0.11.25 behind `host_audio.h`, none in the null build. Since replaced by AVAudioEngine
      (`platform/apple/audio_device_avf.m`) for 3D sound, and removed (decision 19).
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
      Done in two steps: first retail's path without 3D hardware only; then, at the user's request, 3D sound as well:
      `Has3DHW` true, `Use3DCaps` follows "Sound 3D", DirectSound 3D's distance model and doppler in the driver,
      placement with HRTF through AVAudioEnvironmentNode, the listener's velocity derived (decision 19). The
      uninitialised mix values fixed. Checked: `sounddriver` (flags, distance, doppler), `audio3d` (placement on the
      device), `saveload-3d`.
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
      retail, kept (phase 8). Listened to by hand in `12_sol`, both paths, on headphones (decision 19).
- [x] **P4.10** (S) Output device changes while running (headphones in and out).
      Done: AVAudioEngine stops on an output change and the device starts it again. Not done: the check by hand.
- [x] **P4.11** (S) Delete the DirectSound part of `win32_shim_dx.h`.
      Done: and the `dsound.h` and `Mmreg.h` stand-ins.
## Complete Phase 4 Code Audit

Done 2026-11-13; details in `docs/PORTING.md`, "Phase 4 code audit". Extra warnings and the static analyzer on the
phase's code, the newly reachable sound code under ASan and UBSan in every test, and a read-through.

- [x] Warnings and static analysis: four findings in the new code (alignment, a double `getenv`, two in the test),
      fixed; `AudioLib.c`'s remaining reports are retail arithmetic or aligned in every caller.
- [x] Newly reachable retail code: unaligned accesses throughout `AudioLib.c`, the lip-sync overrun, the pause-time
      sign, the uninitialised mix values; all fixed.
- [x] Read-through: the lock discipline between the game and device threads, voice lifetime, clock arithmetic.
- [x] Behaviour: `boot`, `boot-sdl` and the null build write the same autosave; every test passes in the apple, null
      and sanitizer trees.
- [x] By ear: listened to by hand on 2026-11-13 in Solomon's Mines (`12_sol`): the game's own mix on speakers and on
      headphones, then 3D sound with AirPods and head tracking; panning, distance and 3D placement all sounded right.
- Not yet done by ear: the exit test's own levels (`00` to `02`) through a save and load (`saveload` and
  `saveload-3d` cover the save state), and headphones plugged in or out while playing (no test covers it).
- Left open, each with its phase: retail's restored channel handles and hard-right pan, OpenJones3D's sound fixes
  (phase 8 fix series). The DirectSound 3D path, left out at first, was added afterwards (decision 19).
### Phase 5: Movies

Exit test: the five intro movies and every cutscene movie in the GOBs play with audio in sync and can be skipped; a
headless decode test passes for all of them.

- [x] **P5.1** (M) Packing audit: INSANE was built with 1-byte struct packing. List the structs whose layout depends
      on it, pack those explicitly and assert their sizes.
      Done: four on-disk layouts packed with their offsets asserted (SHDR's `rate` at 0x0E is the one natural
      alignment moves); five more 64-bit faults fixed on the way, among them `SmushBitmap`'s pitch and `Decode.c`'s
      8-byte `unsigned long` pixels (docs/PORTING.md, "Packing audit").
- [x] **P5.2** (S) `Stdfile.c` and `Stdio.c` over POSIX, going through the host path conversion.
      Done: in place, with 64-bit handles and `J3DHost_fopen`.
- [x] **P5.3** (M) `Timer.c` over a pthread with a mutex, keeping the callback contract the player expects.
      Done: `platform/common/insane_timer.c`, 60 Hz on the monotonic clock, with a lock retail lacked so a freed
      timer's callback is never running.
- [x] **P5.4** (S) `MemCopy.asm`, `MemFill.asm`, `BlitAsm.asm` to C (193 lines).
      Done: `MemCopy.c`, `MemFill.c`, `BlitAsm.c`. Checked: `insaneasm`.
- [x] **P5.5** (M) `DecodeAsm.asm` to C (519 lines): the two LUT decoders and six 555/565/RGBA conversions.
      Done: `DecodeAsm.c`. Checked: `insaneasm`, against a register-level model of each converter for every input.
- [x] **P5.6** (S) `MixerAsm.asm` to C (306 lines).
      Done: `MixerAsm.c`, all eight procedures.
- [x] **P5.7** (L) `Bloc16Asm.asm` to C (797 lines): the Blocky16 block decoder, cross-checked against ScummVM's.
      Its frame offsets are already `ptrdiff_t` (P1.27): the three frames are separate allocations.
      Done: `Bloc16Asm.c`. Checked: all 2,240 codec-2 frames of the five movies equal a port of ScummVM's decoder;
      vectors that point below the picture now read black guard rows instead of the next heap block (decision 18).
- [x] **P5.8** (M) `tests/moviecheck.c`: decode every frame of every movie with no display and checksum the frames.
      Compare with checksums from the Windows build under DREAMM when those can be captured.
      Done: `moviecheck`, about 4 s, with pinned checksums and ScummVM's decoder as the independent check. The DREAMM
      checksums were not captured. It found three more retail faults (uninitialised packet bytes read as a track's
      priority, stale channel state after a restart, unaligned accesses).
- [x] **P5.9** (M) `Dsnd.c`: a streaming source on the phase 4 audio device, fed by INSANE's mixer.
      Done: `platform/common/insane_dsnd.c` into a stream in the sound driver (`sound_stream.h`), 70 ms ahead of
      the play cursor (retail ran 140 to 210 ms ahead). Checked: the intro's sound through the device for its whole
      length (`J3D_AUDIO_DUMP`).
- [x] **P5.10** (S) `JonesMain_IntroMovieBlt32` into the back buffer, scaled to the window. The 555 and 565 variants
      are not needed.
      Done: and the 32-bit mode match without which nothing played; the movie keeps its shape. Runs on a virtual
      clock (headless, record, replay) play no movie.
- [x] **P5.11** (S) Skip, volume (`SmushPlay_SetGlobalVolume`) and subtitle hooks.
      Done: skip by Return, Escape or click through the game's own intro window procedure (checked by input script);
      volume as retail; the movies carry no subtitles and SmushPlay has no hook for them.
- [x] **P5.12** (S) List every movie in the GOBs with `gobtool.py` and play each from its in-game trigger.
      Done: five movies, all loose intro files, none in the GOBs. Four play from their trigger (640 or 800 wide,
      performance level below or at least 3); `jonesopn_5.snm` needs a 512×384 mode the Mac does not offer and is
      covered by `moviecheck`.
## Complete Phase 5 Code Audit

Done 2026-11-13; details in `docs/PORTING.md`, "Phase 5 code audit". Extra warnings and the static analyzer on the
phase's code, the whole player under ASan, UBSan and `MallocScribble`, a read-through of its three threads.

- [x] Warnings and static analysis: three findings (a mistyped driver slot, unaligned stores in two unused codecs,
      unset edge tables for an impossible glyph size), fixed; the remaining casts point into aligned buffers.
- [x] Newly reachable retail code: out-of-frame motion vectors, uninitialised packet bytes, stale channel cursors,
      unaligned fields, the 64-bit layouts; all fixed in the steps.
- [x] Read-through: the sound engine's track table had no lock between the decoder and the mixing callback, which
      only x86's store order made safe; it has one now, with a fixed lock order. Freed timers never call back.
- [x] Behaviour: the `boot` autosave is byte-identical to `main`'s before the phase, null and apple builds; every test
      passes in the apple (15), null (10) and sanitizer (15) trees; the intro plays and skips under the sanitizers.
- Not yet done by hand: watching and listening to the intro (audio/video sync), fullscreen and windowed.
- Left open, each with its phase: the intro movie chosen only for 640- and 800-wide modes (phase 8 fix series);
  `jonesopn_5.snm` unreachable without a 512×384 mode; the DREAMM checksums.
### Phase 6: Launcher, options and dialogs

Rethought on 2026-11-14 (decision 20). The first pass drew every dialog in Windows 98's classic look inside the game
window. That look is dropped: the text was small and hard to read, and the launcher belongs in a window of its own.
The phase now has two kinds of interface:

- **The launcher and its Options window are SwiftUI**, in their own windows, shown before the game window exists.
  They follow the Steam release's launcher (`Jones3D.exe`): its Launch dialog and its Display, Game Play, Sound and
  Control Options dialogs. A mockup of both is in `../launcher-mockup/` (outside this repository because it holds a
  copy of the retail launch picture).
- **The menus the game raises while it runs are SwiftUI as well** (decision 21, 2026-11-16), laid over the game window
  in the launcher's look. `jonesConfig.c`'s entry points ask the platform's menu first and keep their own rules; the
  Win32 dialogs over `j3dgui` (6A) remain, in their Windows 98 look, for runs with no menu (headless, replays, the
  null platform, the tests, `J3D_MENUS=classic`).

Original art is used wherever the player's copy holds it: the launch picture and the dialog icons. Text is drawn
with the system font at a size that reads on a Retina display, never with the bitmap fonts of the Windows build.

Exit test: the launcher starts and resumes the game; every setting can be changed in Options and the game obeys it;
every dialog reachable in a playthrough works and reads clearly; the game can be saved and loaded in-app.

State on 2026-11-16: complete and audited, merged into `main`. The launcher and Options and every in-game menu are
SwiftUI; the Win32 dialogs over `j3dgui` remain for runs with no menu, and `gamedialogs` opens each of them.

#### 6A. Foundation (`platform/common/j3dgui/`) (complete, committed)

- [x] **P6.1** Classify the 254 stubs in `win32_null.c`: dialog API to implement, GDI to replace, dead. The result is
      the shim's scope.
- [x] **P6.2** `tools/rc2table.py`: generate C tables from `Indy3D.rc` (26 dialog templates, control IDs, classes,
      positions in dialog units, strings).
- [x] **P6.3** Window objects: an `HWND` tree with IDs, text, rectangle, enabled, visible and focus state;
      `GetDlgItem`, `SetDlgItemTextA`, `EnableWindow`, `ShowWindow`, `SetFocus`, `MoveWindow`, `GetWindowRect`,
      `InvalidateRect`.
- [x] **P6.4** Modal loop: `DialogBoxParamA` and `EndDialog`, `WM_INITDIALOG`, `WM_COMMAND`, `WM_NOTIFY` and
      `WM_CLOSE` dispatch, a nested loop that pumps SDL events and presents, as `DialogBoxParam` nested a message loop.
- [x] **P6.5** Renderer: draw the dialog over the captured game frame in the back buffer, replacing
      `JonesDialog.c`'s GDI compositing. Done with a GDI over 32-bit surfaces and Core Text, in the Windows 98 look,
      which stays for the fallback (decision 21).
- [x] **P6.6** Controls 1: static text, push button, check box, radio button, group box, with keyboard (Tab,
      Return, Escape) and mouse. Controller navigation, which this step also named, was not done: P6.41.
- [x] **P6.7** Controls 2: list box and combo box with item data (`LB_*`, `CB_*`).
- [x] **P6.8** Controls 3: trackbar (`TBM_*`), the list view, the edit control and the scroll bar.
- [x] **P6.9** Subclassing: `JonesDialog_SubclassDialogProc` and the stored `WNDPROC` chain in `jonesConfig.c`.
- [x] **P6.10** Icons and bitmaps: `Indy3D.rc` names 43 image files that exist only inside the retail executable.
      Read from the player's `Indy3D.exe` resource section at run time, with a plain fallback when it is absent.

#### 6B. Launcher and Options (SwiftUI, `platform/apple/launcher/`)

The launcher writes the same settings the Steam launcher wrote to the registry, through `wuRegistry`'s
`settings.ini`, and the game reads them as it does today. The launcher holds no game logic.

- [x] **P6.26** (S) Decide how the launcher hands over to the game, by trying both: replace the process with the game
      executable (`execv`, as retail's launcher started `Indy3D.exe`; preferred, because the game target stays free
      of Swift and its tests are unaffected), or run the game loop in the launcher's process. Record the choice in
      `docs/PORTING.md`. Hold the SwiftUI code to what macOS 13 offers (the deployment target, P8.14); the mockup's
      `dismissWindow` is macOS 14 and must go. Done 2026-11-16: in the game's process, from `JonesMain_Startup` just before the start-mode switch, in a
      window of its own; the menus had already put Swift in the game target, so `execv` saved nothing. Its choice is
      the start mode (`J3DMenu_RunLauncher`, `docs/PORTING.md`). Only macOS 13 SwiftUI is used.
- [x] **P6.27** (M) Swift in the build: `enable_language(Swift)` for `J3D_PLATFORM=apple` only, a `launcher` target
      built from the mockup's two files, in both the Ninja and Xcode trees. The null and sanitizer trees do not build
      it. The launcher window: the picture, then Start New Game, Resume Play, Help, Options and Quit in the
      original's order, the title bar hidden, fixed size. Done 2026-11-16 in the menus' Swift library (`LauncherWindow.swift`), not a target of its own; built in the
      Ninja trees (the Xcode tree is checked in the audit).
- [x] **P6.28** (S) The launch picture: bitmap 141 (320×240, 8-bit) in the Steam release's `Jones3D.exe`, read from
      the player's copy at run time through the reader P6.10 wrote; nothing is copied into the repository or the
      bundle. The CD-era launcher (`Jones3D_CD.exe`) loads a `.bmp` from disk instead: find where the GOG and CD
      releases keep it. The plain title card when none is found. Done 2026-11-16: bitmap 141 of the Steam release's `Jones3D.exe`, else `Install/Launch.bmp`, which the CD and
      GOG releases ship, both in the data folder; the title card when neither is there.
- [x] **P6.29** (M) Settings bridge: Swift reads and writes `settings.ini` through `wuRegistry`'s C functions (a
      bridging header), under the names the Steam launcher used: `Start Mode`, `Width`, `Height`, `BPP`,
      `Buffering`, `Filter`, `Performance Level`, `Fog`, `Fog Density`, `Mouse Control`, `Joystick Control`,
      `Difficulty`, `Show Hints`, `Default Run`, `Show Text`, `Map Rotation`, `Sound Volume`, `Sound 3D`,
      `Configuration`. Values use the game's encodings (`Sound Volume` is a float stored as four bytes). Options
      writes only on OK. Done 2026-11-16 without a Swift bridge to `wuRegistry`: Options runs inside the game, so the same C functions
      fill and apply it as the in-game menus (`jonesConfig_ShowLauncherOptions`).
- [x] **P6.30** (S) Start New Game and Resume Play: set what the game needs to start a new game or load
      `Last Save Game`, then hand over (P6.26). Resume Play is the default button and names the latest save when one
      exists, and is disabled when none does; the save's level and date come from its header. Done 2026-11-16: Start New Game saves Start Mode 0; Resume Play saves 1 and the game loads `Last Save Game`
      (`JonesMain_GetResumeSave`); the button shows that save's name and date and is greyed with none.
- [x] **P6.31** (M) Options, Display: the resolution list from the port's display modes (P2.3). Find out which of
      Buffering, Performance Level and Mip Mapping the Metal renderer honours; keep those, drop the others, and put
      P8.7's window or fullscreen, vsync and frame limiter here in their place. Fog and its density. Default
      Settings. Done 2026-11-16: the in-game display menu's items and fullscreen or window (`InWindow`). Buffering is not
      honoured by the Metal present path and is left out; vsync and the frame limiter stay with P8.7.
- [x] **P6.32** (S) Options, Game Play and Sound: the difficulty slider with the game's level names (confirm the
      full set against `jonesConfig.c`; the Steam launcher shows Freshman to Senior), Show Map Hints, Map Rotation,
      Default to Run, Show Text; volume and 3D Sound. Done 2026-11-16: difficulty with the string table's six names (JONES_STR_HARD0 to 5; the game allows all six,
      `jonesInventory_GetMaxDifficultyLevel` returns 5), the four switches, volume and 3D sound.
- [x] **P6.33** (M) Options, Controls: the control sets in the user's and the retail `KeySets` folders, the chosen
      one stored as `Configuration`; view a set as one table of action, keys and controller buttons (the original's
      two lists merged); delete a user set; Enable Mouse and Enable Gamepad/Joystick. Partly done 2026-11-16: the launcher's Controls tab chooses the set and switches the mouse and controller.
      Viewing and deleting sets is in the game's control options (P6.20), which the launcher does not open: its menus
      are laid over the game window. Completed 2026-11-16: the Controls tab's Control Options button opens the game's own control-set menus
      (P6.20) over the Options window: view, create, edit and delete sets, with the same rules and files.
- [x] **P6.34** (L) Create and edit a control set in the launcher, written in the retail `.kfg` format: capture a
      key or a controller button for an action, refuse or reassign one already taken. The launcher has no SDL, so a
      captured controller button must be numbered as `stdControl_sdl.c` numbers it; a test holds the two together.
      Until this lands, sets are edited in the game's own dialog (P6.20, P6.21). Open: sets are created and edited in the game's own menus (P6.20, P6.21), as this step allowed. Done 2026-11-16 through the same menus as in the game (one implementation, so the two cannot differ).
      Keys can be captured in the launcher; controller buttons only in the game, since `stdControl` opens the pads
      when the game starts.
- [x] **P6.35** (S) Help: open the manual PDF from the player's data folder when it is there (the Steam release
      ships one), otherwise the port's README. Done 2026-11-16 for the launcher: `manual.pdf` in the data folder (Steam and GOG ship it), greyed without one.
- [x] **P6.36** (S) Keyboard and accessibility: Return and Escape, Tab through every control, arrow keys on the
      sliders and choices, VoiceOver names for the custom-drawn switch, slider and choice bar. Done 2026-11-16: Return and Escape answer every window and menu; the arrow keys move the highlighted
      button in the launcher and every choice menu; the custom switch, slider and choice bar carry the standard
      controls as their accessibility representation (VoiceOver names and adjustments); Tab moves through the
      controls with macOS's keyboard navigation on.
- [x] **P6.37** (S) Tests: settings written by the launcher's bridge are read back by the `boot` test; the
      launcher's `--snapshot` renders of each screen are kept as layout references, at 1× and 2×. Done 2026-11-16: `launcher` (tests/launcher.sh) starts a new game and resumes it through the launcher's
      choices and checks the settings file and the restore; Options opens and closes. Snapshots: `J3D_MENU_SHOT`.

#### 6C. The in-game menus (SwiftUI, `platform/apple/menu/`)

Replanned by decision 21: the restyle of `j3dgui`'s drawing this part first held (dark panels and the system font in
`gui_draw.c`) is dropped; `j3dgui` keeps the Windows 98 look as the fallback.

- [x] **P6.38** (M) The menu host and look: a borderless child window over the game window's content, run as a modal
      session, with the game's last frame dimmed behind it and the menu scaled with the window (0.6 to 2 against a
      960 by 720 layout); the launcher's colours, buttons, switches, sliders and choice bars; the C interface
      (`j3dmenu_ui.h`) and the game's side (`j3dmenu_apple.c`); `J3D_MENU_TEST`, `J3D_MENU_AUTOCANCEL` and
      `J3D_MENU_SHOT` to open and capture every menu unattended. Done 2026-11-16.
- [x] **P6.39** (S) One palette: the launcher and the menus build from the same Swift style files, so the two cannot
      drift. Done 2026-11-16: the launcher, Options and the in-game menus are built from the same `MenuStyle.swift` and
      `MenuControls.swift`, in one Swift library.
- [x] **P6.40** (S) The original art in the menus: the IQ picture with the game's own figures (done for statistics
      and level completed, on a parchment panel because the figures are black); decide for the dialog icons
      (`Indy3D.rc` names 43, P6.10). Read every menu at the smallest window the game allows and at 2×; no text
      clipped, no body text below 13 points at 1×. Done 2026-11-16: the game's art is used where it carries content (the launch picture, the IQ picture with the
      game's figures, the store's item icons, drawn without smoothing); the 43 dialog icons were decoration beside
      the Win32 dialogs' text and are not drawn in the menus. Menus never scale below 13/15, so body text stays at
      13 points or more: over a window smaller than that needs (the smallest is 640 by 480) the menu reaches past it.
- [x] **P6.41** (M) Controller navigation in the menus: move focus with the stick or pad, accept and cancel on the
      buttons the game's key set uses for them. Left out of P6.6. Done 2026-11-16: the controller's south button (A, cross) is Return, the east button (B, circle) Escape,
      the pad and left stick the arrow keys, in every menu and in the launcher; a move repeats while held, and the
      button that opened a menu is not taken as its answer. Read from SDL's gamepads, not the key set.

#### 6D. In-game dialogs, in order of how early the game needs them

These are the dialogs `jonesConfig.c` raises while the game runs, each a SwiftUI menu (decision 21) with its Win32
form kept as the fallback. The options dialogs among them (112, 113, 114, 148, 111) change the same settings as the
launcher's Options window; both must leave `settings.ini` in the same state.

- [x] **P6.11** (S) Message box (template 121). The game already calls it at startup today. Done 2026-11-16.
- [x] **P6.12** (S) Confirmations: exit game (211), save overwrite and save messages (163, 214), purchase message
      (212), insert CD (167, which the data root should make unreachable). Done 2026-11-16: exit and insert CD are
      menus; the purchase question is `J3DMenu_Ask` from the store; 163 and 214 were raised only by the Win32 file
      dialogs' hooks, and the save menu asks the same questions with the same strings.
- [x] **P6.13** (M) Developer dialog (101, 106): level list and options; gates `Start Mode` 2. Done 2026-11-16: it
      stays a Win32 dialog, as a developer's tool raised before the game window exists (the launcher is the player's
      way in); `gamedialogs` opens it. 106 has no caller (linker-discarded in retail).
- [x] **P6.14** (S) Game play options (112). Done 2026-11-16: difficulty with the game's own level names up to the
      highest unlocked, then the four switches; OK applies and saves them as the dialog's OK did.
- [x] **P6.15** (S) Sound settings (113). Done 2026-11-16: the volume, heard when the slider is let go, and 3D sound
      (off and greyed when the driver reports no 3D); retail's hidden reverse-stereo box is left out.
- [x] **P6.16** (M) Display settings (114) and advanced display settings (148), fed by the P2.3 mode list. Done
      2026-11-16 as one menu: the resolutions the dialog listed, performance level, mip mapping, fog and its density,
      Default Settings. Buffering (ignored by the Metal present path), colour depth (one) and the device lists (one
      each) are left out.
- [x] **P6.17** (L) Save game (154) and load game (159): replace `GetSaveFileName`/`GetOpenFileName` with an in-game
      file list, and the GDI thumbnail in `sithGamesave` with a VBuffer. Done 2026-11-16 as the SwiftUI save and load
      menu: the saves with their pictures, most recent first, retail's checks on a name (reserved names, overwrite,
      progress lost); there is no Win32 fallback, so with no menu they cancel. The thumbnail stays a GDI bitmap of
      the port's own GDI, read into the menu as pixels.
- [x] **P6.18** (M) Game over (150), statistics (164), level completed (233). Done 2026-11-16: game over with retail's
      restart save, music and load loop; statistics as a page a level plus the summary (shown first); level
      completed, then the store. The IQ picture is the game's, with the points drawn by its own code.
- [x] **P6.19** (L) Indy's trading post (190): item list, cart, icons, purchase. Done 2026-11-16: the three lists
      with the game's item icons, add (also by double-click), return, clear, total and balance, the Peru map and
      can't-afford messages, and the purchase question on Done or Escape. Buying by hand is untested (the unattended
      test closes menus unanswered).
- [x] **P6.20** (L) Control options (111) with create scheme (116) and edit scheme (115). Done 2026-11-16 as SwiftUI menus: the control sets (the game's own locked and viewed, the player's edited,
      new or deleted), mouse and controller switches; a set as one table of action, keys and controller buttons with
      Add, Change, Remove and Default Settings; the new set's name with retail's checks. The rules are
      `jonesConfig.c`'s, kept on the sets instead of in list boxes. Editing by hand is untested.
- [x] **P6.21** (M) Assign key (117) and reassign key (120), which read raw input from `stdControl`. Done 2026-11-16: the wait for a key takes keys from AppKit (key code to HID usage to DirectInput key) and
      polls the controllers through `stdControl` as the dialog's timer did, with retail's refusals (Escape, -, =, F4,
      the number keys outside the weapons, keys for buttons and buttons for keys) and the reassign question.
- [x] **P6.22** (S) Confirm templates 104, 105 and 118 are unreferenced and leave them out. Done 2026-11-16: no
      code names them (104 and 105 are DirectPlay's join and connection dialogs, 118 an exit dialog); they stay in the
      generated tables, which `dialogs` builds, and nothing opens them.
- [x] **P6.23** (S) Restore retail's `Start Mode`, `Debug Mode` and `Verbosity` defaults, which were changed in
      phase 1 only because no GUI existed. The launcher now sets `Start Mode` (P6.30). Done 2026-11-16 (in `29d36ba`, `wuRegistry_file.c`): the defaults are retail's again, and the tests and tools
      set what they need; the launcher sets the start mode (P6.30).
- [x] **P6.24** (M) `jonesConfig.c` clean-up left from earlier phases: its path handling (128-byte file-dialog
      buffers), its 58 narrowing warnings (then drop its `-Wno-error=shorten-64-to-32` in `CMakeLists.txt`) and its
      four uninitialised locals (`docs/PORTING.md`, "Known open items"). Its fall-off-the-end functions were fixed in
      P1.22. Done 2026-11-16: the 58 narrowings are explicit
      casts marked `// Port: narrowing cast`, and the file's `-Wno-error` is gone; the four locals have defined values
      (decision 18); the host-path buffers still reached (game over's restart save, the last save's lookup, deleted key
      sets) are `J3D_PATHSIZE`. The 128-byte buffers left are in the Win32 file dialogs' hooks, which no longer run.
- [x] **P6.25** (M) Content pass: every dialog compared with a reference screenshot from the Windows build for what
      it holds (every control present, in the same order, with the same text and behaviour), not for its look. Done
      2026-11-16 against the templates in `Indy3D.rc` and the dialog procedures, as no screenshot of the
      Windows build was at hand: the table in `docs/PORTING.md` ("Content pass") lists each dialog's controls and
      every deliberate difference.

## Complete Phase 6 Code Audit

Done 2026-11-16; details in `docs/PORTING.md`, "Phase 6 code audit". Extra warnings and the static analyzer on the
phase's C, every menu, the launcher and every Win32 dialog under ASan and UBSan, a read-through of lifetimes and
nesting.

- [x] Warnings and static analysis: four findings in the new code (a double `getenv`, a DC leak on a failed table
      growth, unzeroed text metrics, a shadowed global) and two unset reads in retail code the menus reach (the HUD's
      save and load items with saving disabled, insert CD's `WM_DESTROY`), fixed.
- [x] Newly reachable code: freed DCs in use, statistics indexes -1 and 17, the store's shared name buffer; fixed in
      the steps.
- [x] Read-through: main thread only; no C pointer kept past its call but the state the C side owns for the modal
      call; nesting saved and restored or guarded. Found: a cancelled edit of a changed control set was kept; fixed.
- [x] Behaviour: the `boot` autosave is byte-identical to `main`'s before the phase; every test passes in the apple
      (18), null (12) and sanitizer (18) trees, with no compiler warning.
- Not yet done by hand: buying in the store, editing a control set and capturing keys and buttons, a real controller
      in the menus, VoiceOver.

### Phase 7: Full-game verification and 64-bit closure

Exit test: all 17 levels completed on the port with no assert, crash or blocker, and no Win32 type outside
`platform/`. The second half was replaced by decision 22: the Win32 and DirectX names the engine keeps by design stay,
declared by the port, and nothing declared silently does nothing.

State on 2026-11-17: code complete and audited (`docs/PORTING.md`, "Phase 7"). Eight steps are done; the playthrough
(P7.4) and the A/B against the Windows build (P7.5) are by hand and open, as are the base M1's frame times (P7.9).

- [x] **P7.1** (S) One command that configures, builds, runs `ctest` and `layout_audit.py`; run it before every
      commit. Done 2026-11-17: `tools/check.sh` (apple, null and sanitizer trees, `--asserts` adds the debug-asserts
      tree), failing on any warning but SDL3's linker one. It found the sanitizer tree's `launcher` test hung (a counted
      run lost focus and waited; it now stays active).
- [x] **P7.2** (M) Savegame round trip (P1.20) for every level. Done 2026-11-17: `saveload-NN_level` for all 17. Found
      and fixed: restored timed events fired late or early by the load time; an actor whose template health is clamped
      gained health on every restore (Volodnikov, Nub's Tomb); restored loops lost their pitch; a lip-sync read past a
      voice line (ASan). Restored sound channels are matched by guid (retail reorders them).
- [x] **P7.3** (M) Build with `J3D_DEBUG_ASSERTS=ON`, run the tests and a session per level, fix what asserts.
      Done 2026-11-17: the debug-only code needed 29 compile fixes (formats, narrowings, two missing returns, an
      `sscanf` into a `size_t`) and `sithWrite.c`; then every test passes and `tools/level_sessions.sh` ran five
      minutes of every level with no assert, and again under ASan and UBSan with no report.
- [ ] **P7.4** (L) Playthrough, recorded in the table below. Rendering checked 2026-11-17 (screenshots of every
      level); completing the levels is by hand and open.
- [ ] **P7.5** (M) A/B against the Windows build in DREAMM for the physics-sensitive places: long jumps, whip swings,
      raft and jeep handling, the Marduk statue push that 1.2 fixed (risk 7.1). Include the end of every item push
      and pull: retail's final step used an unset direction, which the port sets to the push direction (audit). Open:
      needs the Windows build in DREAMM and a player; `--record` and `--replay` (P3.10) make the port's side repeatable.
- [x] **P7.6** (M) Delete `win32_shim_dx.h` (489 lines) once phases 2 to 5 have removed its last users. Done
      2026-11-17 (decision 22): deleted, with six stand-in headers. What the engine still names (the std3D vertex,
      the device descriptions, the `DIK_*` scancodes, `DPID`) is its own `Libs/std/Win95/stdDirectX.h`.
- [x] **P7.7** (M) Delete `win32_shim_gui.h` (1,538 lines) and `win32_null.c` once phase 6 is complete. Done
      2026-11-17 (decision 22): `win32_null.c` is deleted, with every declaration nothing calls (111); the stubs still
      reached were compiled out or made real, and the HUD's Help, which did nothing, opens the manual.
      `win32_shim_gui.h` stays as the API j3dgui implements (decisions 9 and 21).
- [x] **P7.8** (M) Delete `win32_shim.h` (490 lines): no `HWND`, `HDC` or `DWORD` left outside `platform/`. Closed
      2026-11-17 by decision 22: kept, trimmed to what `crt_posix.c` implements and the engine calls (429 lines); the
      dialog shell, the game window procedure and the engine's structures use its types by design.
- [x] **P7.9** (M) Performance and memory: frame times on a base M1, leak check over a level change, a long session.
      Done 2026-11-17 on an M4 Pro: frame times held the 120 Hz refresh (`J3D_FRAME_STATS`); one retail leak fixed, then
      `leaks` reports none over a world reload; an hour of game time with flat memory. The base M1 is open.
- [x] **P7.10** (S) `docs/PORTING.md` covers every phase. Done 2026-11-17: phases 0 to 7; `docs/BUILDING.md` updated.

Playthrough record for P7.4. "Loads" is the `levelload` test; "Renders" the Metal renderer's screenshots of every level
(`tools/level_shots.sh`, 2026-11-17); "Completed" is by hand.

| Level | File | Loads | Renders | Completed |
| --- | --- | --- | --- | --- |
| Canyonlands | `00_cyn` | ✅ | ✅ | ⬜ |
| Babylon | `01_bab` | ✅ | ✅ | ⬜ |
| Tian Shan River | `02_riv` | ✅ | ✅ | ⬜ |
| Shambala Sanctuary | `03_shs` | ✅ | ✅ | ⬜ |
| Palawan Lagoon | `05_lag` | ✅ | ✅ | ⬜ |
| Palawan Volcano | `06_vol` | ✅ | ✅ | ⬜ |
| Palawan Temple | `07_tem` | ✅ | ✅ | ⬜ |
| Teotihuacan | `08_teo` | ✅ | ✅ | ⬜ |
| Olmec Valley | `09_olv` | ✅ | ✅ | ⬜ |
| V.I. Pudovkin | `10_sea` | ✅ | ✅ | ⬜ |
| Meroë | `11_pyr` | ✅ | ✅ | ⬜ |
| King Sol's Mines | `12_sol` | ✅ | ✅ | ⬜ |
| Nub's Tomb | `13_nub` | ✅ | ✅ | ⬜ |
| Infernal Machine | `14_inf` | ✅ | ✅ | ⬜ |
| Aetherium | `15_aet` | ✅ | ✅ | ⬜ |
| Jeep Trek | `16_jep` | ✅ | ✅ | ⬜ |
| Return to Peru (bonus) | `17_pru` | ✅ | ✅ | ⬜ |
## Complete Phase 7 Code Audit

Done 2026-11-17; details in `docs/PORTING.md`, "Phase 7 code audit". Extra warnings and the static analyzer on the
phase's C, the round trip and five-minute sessions of every level under ASan and UBSan and with the debug asserts, a
read-through, and every level compared with `main` before the phase.

- [x] Warnings and static analysis: nothing in the phase's lines; in older code, an unset fade in the HUD's sliding
      inventory items, fixed (decision 18).
- [x] Newly reachable code: the lip-sync read past a voice line, fixed in P7.2.
- [x] Read-through: the restore's earlier game time affects only the relative event times; the help viewer opens only
      in interactive runs.
- [x] Determinism: six levels wrote different saves from run to run (retail: a thing fade's colour and the sound
      channels' environment flags, uninitialised); fixed, with a `determinism` test over every level.
- [x] Behaviour: in every level the log is `main`'s before the phase and the autosave too, but for the bytes that were
      garbage; every test passes in the apple (35), null (29), sanitizer (35) and debug-asserts (35) trees, with no
      compiler warning.
- Not yet done by hand: the playthrough (P7.4), the DREAMM A/B (P7.5), a base M1's frame times, the HUD's Help.
### Phase 8: Mac product polish

Exit test: a notarized `.app` that a player with a retail copy can install and finish the game with, on a machine that
has no developer tools.

- [x] **P8.1** (S) Choose the product name and bundle identifier, avoiding LucasArts and Disney marks (the working
      name is J3DPort). (Port name will be OpenIndy3D). Done: OpenIndy3D, `io.github.jenavieve-rose.OpenIndy3D`
      (`J3D_BUNDLE_ID`); `platform/common/host_product.h`. The folders under `~/Library` take the name, and a
      `J3DPort` folder from an earlier build is moved across on the first run.
- [x] **P8.2** (M) `.app` bundle target: `Info.plist`, an original icon, high-resolution capable, games category, Game
      Mode eligibility. Icon located in the ICNS Art folder. Done: an apple build makes `OpenIndy3D.app`
      (`platform/apple/bundle/Info.plist.in`), with `jones3d` beside it as a link to its executable. The icon is made
      at build time (`tools/make_icns.sh`) from `J3D_ICON_SOURCE`, by default the picture in `ICNS Art/`; that picture
      is the retail box art, so it is not committed and a distributed build needs an original one (see P8.13).
- [x] **P8.3** (M) Ship dependencies inside the bundle (SDL3 static or embedded); nothing loaded from Homebrew paths.
      Done: SDL3 3.4.20 is built from its pinned release as a static library (`FetchContent`, hash-checked), for
      macOS 13; Homebrew's needs the macOS it was built on. The app links only system libraries.
      `J3D_SYSTEM_SDL3=ON` takes an installed SDL3 for development.
- [x] **P8.4** (M) First-run data locator: look in the Steam, GOG and DREAMM install locations, otherwise an open
      panel; store the result as `Data Root`. The launcher (6B) shows the result and asks when nothing is found.
      Done: `platform/common/host_data.c` searches Steam's libraries, DREAMM's `lec-infernal` editions, the usual Mac
      folders and CrossOver and Whisky bottles, before the launcher; with nothing found an alert and an open panel ask
      (`J3DApple_AskForData`), and holding Option at launch asks again. The launcher's status line names the folder
      and version. Tested: the Steam copy is found here; `launcher`'s `nodata` case answers the question.
- [x] **P8.5** (S) Validate the chosen folder: GOB names and sizes, and the `INDY3D.EXE` hash against
      `docs/VERSIONS.md`. Decide and state which data versions are supported (only 1.2 is tested so far).
      Done: the three GOBs are required in `Resource` (GOG's `CD1.GOB` is its own size); the demo is refused. Supported
      and tested: English 1.2, as Steam, GOG, DREAMM and the LucasArts update have it. 1.0, 1.1, another language or
      an unknown executable are accepted with a warning. `datacheck` test.
- [x] **P8.6** (S) Command line: `--level`, `--windowed` (`--data` exists). Done: `--level <1-17>` starts a new
      game there without the launcher; `--windowed`, `--fullscreen` and `--help`.
- [x] **P8.7** (S) Display preferences: window or fullscreen, vsync, frame limiter. Their controls are in the
      launcher's Options (P6.31). Done: `VSync` (default on) and `Frame Limit` (none, 30, 60 or 120), on the Display
      tab. Measured: a 30 limit holds 33.33 ms frames. Vsync off matters only full screen; in a window macOS composites
      at the display's rate (8.33 ms here with it off).
- [x] **P8.8** (M) Triage OpenJones3D's `CHANGELOG.md` (~80 entries) into crash fixes, raised limits and quality of
      life, each with its upstream commit. Done: `docs/UPSTREAM-FIXES.md`, against upstream's history (`a675b39`):
      each entry applied, already right in the decomp, already fixed by the port, not applicable (code the port
      replaces), not applied (quality of life, behaviour changes) or open, with its commit.
- [x] **P8.9** (L) Apply the series behind a build option that defaults on, in groups, each retested: crash and
      memory fixes; COG stack and sound-handle fixes; raised limits (128 sounds, mesh and sector limits); widescreen
      and HUD scaling. Done: `J3D_FIXES` (default on) holds the fixes, each marked `Fix (OpenJones3D <commit>)`
      (`docs/UPSTREAM-FIXES.md`); 128 sounds and 4,096 visible sectors (the mesh limits are not reached by retail
      data); restored sound handles equal the saved ones. Widescreen: a picture wider than 4:3 shows the 4:3 height
      and more at the sides (retail cut the height), the indicators stay round, the inventory-changed item keeps its
      place; 4:3 is unchanged. Checked with 1920x1080 shots of every level, the inventory and the map
      (`J3D_HEADLESS_SIZE`); the display test pins the focal lengths. Every test passes with the fixes and with
      `J3D_FIXES=OFF`.
- [x] **P8.10** (S) Keyboard and controller glyphs in the control dialogs. Done: a control set's chips show SF
      Symbols for a gamepad's buttons, d-pad, sticks and triggers, the mouse, and the arrows, modifiers and editing
      keys (`J3DMenuUI_ControlGlyph`), with the name as tooltip and for VoiceOver; letters, digits and a plain
      joystick's numbered buttons keep their names, as does any symbol the running macOS lacks.
- [x] **P8.11** (S) Crash handling: write the engine log beside the crash report and tell the player where it is.
      Done: `platform/common/host_crash.c` keeps the engine's last 64 KB of output whatever the log mode; on a crash
      signal it writes them with a backtrace to `~/Library/Logs/OpenIndy3D/Crash.log` and lets the signal end the
      game, so macOS still writes its report. The next run says where the log is (an alert with Show in Finder).
      `crash` test; the launcher test covers the alert.
- [x] **P8.12** (S) README and in-app notices: a retail copy is required, AGPL source offer, credits. Done:
      `README.md`; the About window's `Credits.html`, with `LICENSE` and `NOTICE` in the app; `NOTICE` credits SDL 3,
      now built in. The first-run question says a retail copy is needed.
- [ ] **P8.13** (M) Hardened Runtime, code signing, notarization, and a disk image. Tooling done: `tools/package.sh`
      signs with the Hardened Runtime (no entitlements needed) and a timestamp, refuses a non-system library, makes
      and signs the disk image, and notarizes and staples it given a `notarytool` profile. Checked with an ad-hoc
      signature: the app runs from the image (windowed, Metal, a level saved). Open: this Mac has no Developer ID, so
      no notarized image yet, and the icon is the retail box art, to replace before a public release.
- [ ] **P8.14** (S) Clean-machine test on macOS 13 (the deployment target) and on the current release. By hand,
      after P8.13: the binary's minimum is 13.0 and it links only system libraries; a Mac on 13 is needed to run it.

Phase 8 is on `main` (at `5ee28b6`), merged by fast-forward on 2026-11-17: seven commits from the branch
`phase8-polish`: the name, bundle and static SDL3 (`d0dc5e6`), the data locator, command line and display preferences
(`596c013`), OpenJones3D's fixes (`6423ed5`), glyphs and the crash log (`7df66e8`), notices and packaging
(`07377b3`), the audit (`2f0872e`) and widescreen (`5ee28b6`). At `5ee28b6` everything builds and every test passes
in the apple (37 tests), null (31), sanitizer (37) and debug-asserts (37) trees (`tools/check.sh --asserts`), and in a
null tree with `J3D_FIXES=OFF` (31); the commits before it were not each rebuilt. Not pushed.

## Complete Phase 8 Code Audit

Done 2026-11-17; details in `docs/PORTING.md`, "Phase 8 code audit".

- [x] Warnings and static analysis: no warning in any tree (the SDL3 linker warning is gone); the analyzer on the
      phase's code found `J3D_DATA` read twice (now once); its other reports are in older lines.
- [x] Read-through: the crash handler is async-signal-safe and re-raises; the print tap does not wrap twice; the
      player resets run while the world exists; the save format is unchanged but for the attachment fix's -1.
- [x] Both builds of the fixes: `J3D_FIXES=OFF` passes every test with retail's behaviour (null tree, 30 tests).
- [x] Behaviour: every test passes in the apple (37), null (31), sanitizer (37) and debug-asserts (37) trees, layout
      audit 0; the packaged app runs from its disk image under the Hardened Runtime.
- [x] Widescreen (P8.9, after the audit): 4:3 unchanged by construction; 1920x1080 shots of every level checked.
- Open: Developer ID signing, notarization and an original icon (P8.13), the macOS 13 clean-machine test (P8.14).

### Phase 9: Stretch (not counted)

Universal binary; higher-resolution textures from loose files; MSAA and anisotropic filtering; an SDL_GPU backend for
Linux and Windows; offering the portable platform layer to OpenJones3D. Started 2026-11-17 on the branch
`phase9-stretch`; details in `docs/PORTING.md`, "Phase 9: stretch goals". Not counted in the table at the top.

- [x] **P9.1** (M) Universal binary: one app for Apple silicon and Intel Macs. Done: `tools/universal.sh` builds an
      arm64 and an x86_64 tree (Ninja cannot build Swift for two architectures in one) and joins them with `lipo` into
      `build-universal/OpenIndy3D.app`; `tools/package.sh build-universal` packs it. Textures the CPU touches are
      managed on a GPU without Apple's shared storage (`J3DMetal_TextureStorageMode`), read back after a
      `synchronizeResource` blit. Checked: both trees build with no warning and pass every test, the x86_64 one under
      Rosetta 2; `render-managed` runs the renderer checks on the managed path. Open: a run on an Intel Mac.
- [x] **P9.2** (M) Higher-resolution textures from loose files. Done: `Resource/hires/<material>.png` (cel 0) or
      `<material>_cel<n>.png` replaces a cel's texture at any size, with its alpha and a GPU-made mip chain; the game
      keeps the material's size (`std3D_LoadHiresTexture`, `texture_image.m`). `J3D_HIRES_DUMP` writes the originals
      as a starting set; the `Hires Textures` setting turns it off. Checked: `render`'s PNG round trip; `00_cyn` with
      all 964 cels replaced at 4x draws them in place; no cost without a `hires` folder.
- [x] **P9.3** (S) MSAA and anisotropic filtering. Done: settings `MSAA` (2, 4, 8) and `Anisotropy` (to 16), off by
      default, in the launcher's Display page with `Hires Textures` as "Image Quality". The scene draws multisampled
      and resolves into the scene texture, so read-back and present are unchanged. Checked: `render-msaa`, Metal
      validation, `00_cyn` shots at 1280x960 with and without.
- [ ] **P9.4** (L) An SDL_GPU backend for Linux and Windows.
- [ ] **P9.5** (S) Offer the portable platform layer to OpenJones3D.

P9.1 to P9.3 are on `main` (at `a9baf3d`), merged by fast-forward on 2026-11-17: two commits from the branch
`phase9-stretch`, the three steps (`eb03a17`) and the audit with the docs (`a9baf3d`). At `a9baf3d` everything builds
and every test passes in the apple (42 tests), null (34), sanitizer (42) and debug-asserts (42) trees
(`tools/check.sh --asserts`), and in the arm64 and x86_64 trees of `tools/universal.sh --test` (42 each).

## Complete Phase 9 Code Audit (P9.1 to P9.3)

Done 2026-11-17 for the three steps finished; details in `docs/PORTING.md`, "Phase 9 code audit".

- [x] Warnings: none in any tree, x86_64 included.
- [x] Read-through: one fix, an image with alpha for a material without it was drawn premultiplied and came out dark
      (now always decoded with alpha and unpremultiplied; `render` checks it). The lookups' 0.1 s a level without a
      `hires` folder was found by measuring and removed.
- [x] A retail bug the x86_64 tree's `determinism` test found: a cog vector literal with names in it
      (`tem_dynalight.cog`, level 7) left the vector's components as stack garbage; zeroed now
      (`sithCogParse_LexerSetVector`).
- [x] Retail picture unchanged by default: MSAA and anisotropy default off, and without a `hires` folder no texture is
      replaced; every test, screenshot and determinism check passes as before.
- [x] Behaviour: every test passes in the apple (42), null (34), sanitizer (42) and debug-asserts (42) trees, layout
      audit 0, and in the arm64 and x86_64 trees of `tools/universal.sh` (42 each, x86_64 under Rosetta 2).
- Open: the app on an Intel Mac; the new launcher controls looked at by hand.

## 6. Repository layout

What exists today, and what each phase adds.

```
j3dport/
  CMakeLists.txt        exists
  LICENSE, NOTICE       exists
  Jones3D/              imported; its Win32 calls are the port's emulation (decision 22)
  Libs/{sith,rdroid,std,sound,smush,zlib,j3dcore,wkernel,w32util,intel}   imported
  platform/apple/       main.c, shim/, host_apple.m, wkernel_sdl.c, stdDisplay_sdl.c, display_metal.m,
                        std3D_metal.c, render_metal.m, stdControl_sdl.c, audio_device_avf.m
                        + bundle and locator (8)
  platform/common/      crt_posix.c, host_path.h, host_clock.h, host_dirs.c, wuRegistry_file.c,
                        stdDisplay_vbuffer.c, stdDisplay_common.c, host_screenshot.c,
                        null/ (with stdDisplay_headless.c)
                        SoundDriver_mix.c, host_audio.h     + insane_os/ (5), j3dgui/ (6)
  tools/                gen_null_platform.py, layout_audit.py, gobtool.py, rc2table.py (6), check.sh,
                        level_sessions.sh (7)
  tests/                harness.c, levelload.c, cogcheck.c, saveload.c, display.c, present.c, render.c,
                        keys.c, input.sh, sounddriver.c, audio3d.c, moviecheck.c (5), determinism.sh (7)
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

The replan of 2026-11-14 adds two risks. The launcher brings Swift into a C and Objective-C build (P6.27), and must
stay within macOS 13's SwiftUI. And the same settings can now be changed in two places, the launcher's Options and
the game's own options dialogs, in two languages; `settings.ini` is the only thing they share, and P6.37 tests it.

### 7.4 Blocky16 and INSANE correctness

Hand-written assembler with no debug build to read, and structures that assume 1-byte packing. Mitigation: the packing
audit first (P5.1), ScummVM's codec as reference (P5.7), and a headless frame-checksum test (P5.8).

### 7.5 Licensing and trademarks

AGPLv3 code, no assets, a product name that avoids LucasArts and Disney marks (P8.1), and a README stating a retail
copy is required. ScummVM-derived code must keep its GPL notices; GPLv2+ is compatible with AGPLv3.

### 7.6 Tooling

SDL3 is stable; Metal and Apple clang are first-party. No deprecated framework is used.

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
| 4 | Audio complete | 2 weeks | Complete 2026-11-13, with 3D sound; listened to by hand in `12_sol` |
| 5 | Movies complete | 2 to 3 weeks | Complete 2026-11-13, audited: the intro plays with sound; every movie decodes headlessly; watching by hand open |
| 6 | Launcher, Options, all dialogs, save and load in-app | 4 to 6 weeks | Complete 2026-11-16, audited: SwiftUI launcher, Options and in-game menus, Win32 dialogs as the fallback; using them by hand open |
| 7 | Full playthrough verified | 2 to 3 weeks | Code complete 2026-11-17, audited: every level saves and restores, renders, runs with the asserts and the sanitizers; the playthrough and DREAMM A/B by hand open |
| 8 | Signed `.app`, data locator, fixes | 2 to 3 weeks | 12 of 14, audited 2026-11-17: the app, data locator, OpenJones3D's fixes and widescreen, crash log, packaging; Developer ID notarization and the macOS 13 test open |

Ordering: phase 2 needs 1C (P1.17, P1.18) but not 1D. Phases 3, 4 and 5 are independent of each other once phase 2
lands. Phase 6 needs phases 2 and 3. Phase 7 needs everything before it.

## 9. Immediate next steps

1. **By hand, before phase 7 relies on them:** the phase 3 playthrough of `00_cyn` (keyboard and mouse, then a
   controller), and for phase 4 the remaining checks by ear: the first three levels through a save and load, and
   headphones plugged in or out while playing (the sound itself was listened to and approved in `12_sol`).
   `J3D_AUDIO_DUMP` (BUILDING.md) records what was heard, for comparing later builds.
2. **Phase 5, by hand:** watch the intro for audio/video sync and listen to it (`moviecheck` measures the sound but
   nobody has listened), fullscreen and windowed.
3. **Phase 6, by hand:** start a game from the launcher and resume one; buy in the store; create and edit a control set,
   capturing keys and controller buttons; drive the menus with a controller; read them with VoiceOver.
4. **Phase 7, by hand:** the playthrough (P7.4), level by level in the table, under the sanitizer build where it can
   be; the A/B against the Windows build in DREAMM (P7.5); `J3D_FRAME_STATS=1` on a base M1 (P7.9).
5. Run `tools/check.sh` before every commit (P7.1); it keeps the sanitizer tree in the routine.

To reproduce the measurements at the top of this file:

```
tools/check.sh --data <folder holding RESOURCE/> --asserts
```

When a step is finished, tick it here and update its row
in the table at the top of this file.
