# Indiana Jones and the Infernal Machine: Apple Silicon Port Plan

Status: draft 1, 2026-11-11. Covers what is in `~/Developer/Indy3D_Mac` today.

## 1. Goal

A native arm64 macOS application that plays the full retail game (all 17 levels, cutscene movies, saving and loading, the in-game menus and store) from the player's own copy of the game data, with no Windows executable, Wine, Rosetta, or DLL injection involved.

Non-goals for v1.0: multiplayer (the game has none), the level editor paths, savegame compatibility with Windows saves, Intel Mac support (cheap to add later as a universal binary, but not a milestone).

## 2. What we have to work with

| Item | Location | What it is | Verdict |
| --- | --- | --- | --- |
| Byte-exact decompilation | `infernal-machine-decomp-public-main/` | 100% source for `Indy3D.exe` v1.2 and v1.0, ~188k lines of C, plus 7.5k lines of zlib 1.0.9 and the INSANE movie player | **The code base for the port.** Complete, including the 448 functions OpenJones3D still thunks into the retail exe. |
| OpenJones3D v0.3.1 | `OpenJones3D-main/` | Partial reimplementation (87%) that runs by injecting `Jones3D.dll` into a suspended `Indy3D.exe` v1.0 | **Not runnable on macOS and not a base.** Mine it for ~80 catalogued bug fixes and quality-of-life changes (CHANGELOG.md) and for its CMake layout. |
| Retail game data | `Indy3D/RESOURCE/` | `CD1.GOB` (353 MB), `CD2.GOB` (438 MB), `JONES3D.GOB` (49 MB), five `.snm` intro movies, 51 patched `.cog` scripts in `Cog/`, `KEYSETS/*.kfg` | Complete v1.2 install. `INDY3D.EXE` hashes to `55fb00c0…` = official 1.2 update, so the decomp's default build matches it exactly. A second identical copy lives in the DREAMM install folder. |
| Top-level `JONES3D.EXE` | `Indy3D/` | LucasArts' 77 KB launcher | Irrelevant. |

Both source trees are AGPL v3. The port inherits that license. No game assets may be shipped; the app must locate the player's own data.

### 2.1 Where the Windows dependence actually lives

Measured on the decomp tree. The engine core is far more portable than its size suggests.

| Module | Lines | Win32 / DirectX exposure | Port action |
| --- | --- | --- | --- |
| `Libs/sith` (game logic, COG VM, AI, physics) | 104,110 | 6 stray calls in 4 files (registry, GDI thumbnail in savegame, console) | Keep as-is. Fix stragglers. |
| `Libs/rdroid` (renderer front end, math) | 19,762 | None, except `LPD3DTLVERTEX` in one struct and the SSE "Katmai" mesh path | Keep. Replace the vertex typedef, drop Katmai. |
| `Libs/std/General` | 5,781 | x87 inline asm in `std.c` and `stdMath.h` (already has a `NONMATCHING_MATH` C fallback), backslash path handling | Small edits. |
| `Libs/std/Win95` | 9,768 | 100%: `std3D.c` (Direct3D 6, 2,622 lines), `stdDisplay.c` (DirectDraw, 3,214), `stdControl.c` (DirectInput, 1,346), `stdComm.c` (DirectPlay, 936), `stdConsole.c`, `stdWin95.c`, `stdGuid.c`, `stdGob.c` | **Rewrite** behind the existing headers. `stdGob.c` is portable file I/O and stays. `stdComm.c` becomes a stub. |
| `Libs/sound` | 6,713 | `Driver.c` (DirectSound + DS3D, 1,264 lines) is 100% Windows; `Sound.c` (3,377) and `AudioLib.c` are portable apart from the `LPDIRECTSOUNDBUFFER` handle type and one inline-asm loop | Rewrite `Driver.c`; typedef the handle. |
| `Libs/smush` (INSANE movie player) | 10,267 | 6 MASM `.asm` files, `Sound/Dsnd.c` (DirectSound), `Timer/Timer.c` (Win32 thread + critical section), `File/Stdfile.c` | Port asm to C, rewrite the three OS files. |
| `Libs/wkernel`, `Libs/w32util` | 692 | 100% (window class + message pump, registry) | Rewrite, tiny. |
| `Jones3D/` (game shell) | 23,318 | `jonesConfig.c` is 9,952 lines of Win32 dialog procedures (547 Win32 references); `JonesDialog.c`, `JonesDevDialog.c` and `Indy3D.rc` define 26 dialog templates; `JonesMain.c` has `WinMain` and the movie blitters | The single biggest porting item. See section 5.6. |
| `Libs/zlib` | 7,512 | None | Replace with system zlib or keep vendored (it is modified to read through INSANE's file table; keep it). |

Other facts that shape the design:

- The GPU is only a rasterizer. `rdroid` transforms, clips, lights and projects on the CPU and hands `std3D_DrawRenderList` pre-transformed `D3DTLVERTEX` triangles (screen x, y, z, 1/w, diffuse, specular, u, v) with a texture and a small render-state bitmask (bilinear, clamp U/V, z-write off, fog, alpha-ref). A Metal backend therefore needs one pipeline family, not a scene graph.
- 2D (HUD, fonts, console, dialogs, movie frames) writes pixels straight into a locked back buffer (`stdDisplay_VBufferLock`, `pPixels`). The port must provide a CPU-side back buffer that is composited onto the Metal drawable each frame.
- Key bindings are stored as DirectInput `DIK_*` scancodes (187 uses in `jonesConfig.c`, 59 in `sithControl.c`) and the `.kfg` keyset files name keys by DirectInput names. The input layer must produce DIK codes, not SDL scancodes.
- Settings live in the registry under `HKLM\Software\LucasArts…\v1.0` via 10 `wuRegistry_*` functions, called from 10 files.
- Asset lookup (`JonesFile.c`) searches three roots: current directory, "Install Path" and "Source Dir" (the CD), and opens `CD1.GOB`/`CD2.GOB` from the CD root and `JONES3D.GOB` from the install root. Loose files (the 51 patched cogs) override GOB contents.
- `Libs/j3dcore/types.h` redefines `int32_t`, `uintptr_t` (as `unsigned int`) and friends to reproduce the 1999 ABI. This header must be replaced before anything compiles on a 64-bit toolchain.
- Binary formats are read as raw structs: 13 `pFileRead` sites in `sithThing.c`, 10 in `sithWeapon.c`, the CND level loader, the NDS savegame header, `.mat` and font files. Any struct that crosses a file boundary and contains `size_t`, a pointer, or a pointer-holding union changes layout on 64-bit. Concrete example: `SithCogValue` is a union of `void*`, `float`, `int`, `char*` and `rdVector3`. On 32-bit it is 12 bytes, 4-aligned; on arm64 the pointer members make it 16 bytes, 8-aligned, and the savegame header that embeds an array of them silently changes.

## 3. Strategy decisions

1. **Base the port on the decomp, not on OpenJones3D.** The decomp is complete, matches the player's 1.2 executable, and the engine core is already nearly platform-neutral. OpenJones3D's remaining 13% is exactly the physics, player-control and AI code that would otherwise have to be written from scratch.
2. **Hard-fork into a new repository with the same file layout.** Upstream is finished (byte-exact), so churn is low, and the port needs structural edits (types, headers, removed Win32 types) that cannot be done as overlay patches. Record the upstream commit so fixes can still be cherry-picked. Keep LucasArts' module and function naming so OpenJones3D and OpenJKDF2 knowledge transfers directly.
3. **64-bit arm64 native from day one.** Do not attempt a 32-bit build; macOS has none. Accept the audit cost described in 5.1.
4. **Replace, do not wrap, the platform layer.** Keep the public headers of `std3D`, `stdDisplay`, `stdControl`, `SoundDriver`, `wkernel`, `wuRegistry` (minus the DirectX types in their signatures) and write new `.c`/`.m` implementations in a `platform/apple/` tree. The engine above them does not change.
5. **Graphics: Metal directly, through a small Objective-C shim with a C interface.** Rationale: OpenGL is deprecated on macOS and the interface we must satisfy is ~25 functions over a fixed vertex format. SDL3's GPU API is the fallback if portability to Linux/Windows becomes a goal; the std3D replacement should be written so the Metal calls sit in one file.
6. **Window, input, timing, gamepads: SDL3.** It handles the window and CAMetalLayer, relative mouse mode, keyboard scancodes, and gamepads through Game Controller framework. A pure AppKit implementation is possible but costs more and gains nothing for a game window.
7. **Audio: miniaudio** (single-file C, public domain / MIT-0, Core Audio backend, built-in 3D spatializer, pitch and pan). It maps cleanly onto the `SoundDriver_*` API, which is a thin DirectSound buffer wrapper with 3D positioning. OpenAL-soft is the alternative if the spatialization sounds wrong.
8. **Movies: port INSANE, do not swap in ScummVM.** The game drives the player through `SmushPlay_PlayMovie` with a blit callback into its own back buffer; keeping INSANE preserves timing, subtitle hooks and audio mixing behaviour. The six assembler files become C. ScummVM's `Blocky16` codec (GPLv2+) is a legal, proven reference for the hardest one.
9. **GUI: emulate the slice of the Win32 dialog API that `jonesConfig.c` uses**, backed by our own widget toolkit rendered with the engine's font and 2D primitives. This keeps ~10k lines of dialog logic (control configuration, store, save/load, chapter complete, options) intact rather than rewriting it. Details in 5.6.
10. **Build with CMake; generate the Xcode project from it.** OpenJones3D's CMake tree is the starting point. The `.app` bundle, signing and notarization are Xcode/CMake targets. The decomp's ninja/python matching pipeline is not used by the port.
11. **Bring over OpenJones3D's fixes as a curated patch series, after the game runs**, not before: applying them first would blur the line between "port bug" and "behaviour change". Each fix has a commit hash in its CHANGELOG, which makes the series auditable.

## 4. Target architecture

```
Jones3D app shell (JonesMain, HUD, inventory, dialogs' logic)      unchanged C
        │
sith (game) ── rdroid (renderer front end) ── sound/Sound.c        unchanged C
        │              │                          │
   platform/apple (new)                           │
   ├─ std3D_metal.m      Metal rasterizer for pre-transformed tris
   ├─ stdDisplay_sdl.c   window, modes, CPU back buffer, present
   ├─ stdControl_sdl.c   keyboard→DIK map, mouse, gamepads
   ├─ SoundDriver_ma.c   miniaudio voices, 3D listener
   ├─ wkernel_sdl.c      event loop, window proc shim
   ├─ wuRegistry_plist.c settings file
   ├─ j3dgui/            Win32-dialog emulation + widget renderer
   ├─ insane_os/         timer thread, file I/O, audio sink for SMUSH
   └─ AppDelegate.m      bundle, data-folder picker, fullscreen, Retina
```

Threading model stays as the original: one game thread, plus INSANE's timer thread and miniaudio's callback thread. No attempt to parallelize.

## 5. Work breakdown

Each phase ends with a demonstrable milestone. Estimates are for one experienced engineer working full time and are deliberately rough.

### Phase 0: Bootstrap (1 week)

- Create the port repository; import the decomp tree at a recorded commit; add `LICENSE` (AGPLv3), `NOTICE` crediting LucasArts, the decomp authors, smlu/OpenJones3D and OpenJKDF2.
- CMake project: targets `j3dcore`, `std`, `rdroid`, `sith`, `sound`, `smush`, `zlib`, `jones3d` (app). Toolchain: Apple clang, C11, `-fno-strict-aliasing`, `-fwrapv`, `-ffp-contract=off` (see 7.1), warnings as errors for `-Wint-to-pointer-cast -Wpointer-to-int-cast -Wshorten-64-to-32`.
- Replace `j3dcore/types.h` with `<stdint.h>`/`<stddef.h>`; strip the count-fit (`CF_*`) placeholder macros, which exist only to pad the 1999 compiler's symbol tables.
- Replace `j3d.h`'s `_WIN32` branch: `J3DAPI` becomes empty, `strcmpi`→`strcasecmp`, `J3DNORETURN`→`_Noreturn`, `__inline`→`inline`.
- Add `platform/apple/win32_shim.h`: typedefs for `BOOL`, `DWORD`, `HANDLE`, `RECT`, `HWND` etc. used by otherwise-portable code, so compilation can proceed before every reference is cleaned. Track its contents as debt; the goal is to delete it.
- Stub every `std/Win95`, `sound/Driver`, `wkernel`, `w32util` and `smush` OS function so the whole tree **links** (a "null platform"). Milestone: `jones3d` links on arm64 and prints the engine's startup log before failing on display open.

### Phase 1: Headless boot and the 64-bit audit (2 to 3 weeks)

- **Filesystem and VFS.** Make `stdFnames` accept and emit `/`; normalise `\` on input from scripts and level files. Make loose-file lookup case-insensitive (GOB lookups already go through a hash; loose `Cog/` overrides and user data must still resolve on a case-sensitive volume). Add a "game data root" setting pointing at a folder containing `RESOURCE/` (the player's install or the DREAMM copy); JonesFile's three roots map onto it.
- **Settings.** `wuRegistry_*` over a key-value file in `~/Library/Application Support/<AppName>/settings.ini` (or a plist via CFPreferences). Preserve key names so OpenJones3D documentation still applies.
- **Host services.** `stdPlatform` time source (`mach_absolute_time`), assert, printf to stderr and log file; `JonesError.txt` in the data root.
- **64-bit audit.** Enumerate every raw `pFileRead`/`pFileWrite`/`stdConffile_Read` of a struct and every `memcpy` into on-disk layouts (CND level loader in `sithWorld`/`sithSector`/`sithThing`/`sithSurface`/`sithWeapon`/`sithMaterial`/`sithPVS`, NDS savegame header, `.mat`, `.key`, font `.gcf`). For each, introduce an explicit packed on-disk struct with fixed-width fields and convert into the in-memory struct. The `SithCogValue` union and the `aCogGlobalValues` array in `NdsHeader` are the first known case. Grep for `int` fields that receive pointers (52 candidate casts flagged already) and for `sizeof` of in-memory structs used as file sizes.
- **COG parser.** The checked-in yacc/flex output compiles with clang. Verify by compiling all 51 loose cogs plus every `.cog` extracted from the GOBs in a test harness.
- Milestone: with a null renderer, the game starts, loads `JONES3D.GOB`, parses the string tables, reaches the title/menu state machine, and a test harness loads every `.cnd` level from the GOBs without error.

### Phase 2: Display and renderer (3 to 4 weeks)

- **`stdDisplay`** over SDL3: enumerate modes (report the native display modes and a window option; the engine's 64-mode cap is one of the things GOG patched), create the window with a `CAMetalLayer`, allocate the CPU back buffer (32 bpp BGRA; OpenJones3D already removed the 16 bpp paths) and implement `VBufferLock/Unlock/Fill/Copy/ColorKey`, `Update` (present), `SaveScreen`, `LockBackBuffer`. `GetBackBufferDC`/`FlipToGDISurface` are only used by `JonesDialog.c` and are replaced in Phase 5.
- **`std3D`** over Metal: one render pipeline per blend/fog/alpha-test combination, cached; a dynamic vertex buffer ring; texture upload from `tSystemTexture` mip chains (RGBA8); sampler states for bilinear/nearest and clamp/wrap; depth buffer with z-write toggle; fog as a per-draw uniform using the engine's start/end depths; viewport and projection from `std3D_SetProjection`. The vertex shader converts the pre-transformed screen coordinates (x, y, z in 0..1, rhw) to clip space; the fragment shader multiplies texture by diffuse and adds specular, matching D3D6 fixed-function.
- **Compositing.** Each frame: clear, draw the 3D render lists, then blit the CPU back buffer (HUD/text/dialogs) as a textured full-screen quad with colour-key transparency where the engine used it. Decide per the engine's actual draw order whether 2D is drawn before or after 3D in each state (menus draw over a frozen 3D frame, for instance).
- **Katmai.** Remove `rdModel3K*` and the SSE path; `rdModel3` already has the portable mesh path (`rdModel3K_IsEnabled()` gate).
- **Retina and aspect.** Render at native pixel size; apply OpenJones3D's widescreen HUD fixes later (Phase 6). Keep 4:3 pillarboxing as a fallback option because the HUD layout assumes it.
- Milestone: the first level renders and the camera can be flown with a debug input path; HUD text appears; `stdDisplay_SaveScreen` works.

### Phase 3: Input (1 to 2 weeks)

- **Keyboard:** SDL scancode → DIK table (the DirectInput codes are the PC set-1 scancodes, so the table is mechanical). Preserve `stdControl_ReadKey` semantics (pressed-this-frame edge detection) and the "keyboard foreground" flag.
- **Mouse:** relative mode when controls are active; sensitivity scaling as the engine expects raw DirectInput deltas; show/hide cursor.
- **Gamepads:** SDL_Gamepad axes and buttons mapped into the engine's slot scheme (8 devices, 6 axes, 32 buttons, 4 POV hats). Ship a default `.kfg` for a standard dual-stick controller alongside the retail keysets.
- Milestone: the first level is playable with keyboard and mouse and with a controller; the key-name display (`GetKeyNameTextA` in 1.2's `jonesConfig`) returns names from our own DIK name table.

### Phase 4: Audio (2 weeks)

- Implement `SoundDriver_*` over miniaudio: engine init, output format, per-voice create/play/stop/volume/pan/frequency/position, listener orientation and `ListenerCommitDeferred`, global distance/rolloff/doppler factors, `Has3DHW` reporting true. Typedef the `LPDIRECTSOUNDBUFFER` handle in `sound/types.h` to an opaque `SoundVoice*`.
- Port the one inline-asm loop in `AudioLib.c` (sample conversion) to C.
- Verify `sithSoundMixer`, voice lines with subtitles (`sithVoice`), looping ambient sounds and the save/restore of sound handles (OpenJones3D fixed bugs here; take those fixes).
- Milestone: music, ambience, effects, 3D positioning and dialogue work in the first three levels.

### Phase 5: Movies (2 to 3 weeks)

- Port the six MASM units to C: `MemCopy`, `MemFill`, `BlitAsm` (trivial), `DecodeAsm` (LUT and 555/565 conversions), `MixerAsm` (buffer copy/fill/LUT loops), `Bloc16Asm` (the Blocky16 block decoder; cross-check against ScummVM's implementation and against frame checksums produced by the Windows build).
- Replace `Timer.c` (pthread with elevated QoS and a mutex), `Stdfile.c`/`Stdio.c` (POSIX), `Dsnd.c` (a streaming miniaudio data source fed by INSANE's mixer).
- The game's blit callbacks (`JonesMain_IntroMovieBlt*`) already write into the locked back buffer; only the 32 bpp one is needed.
- Milestone: the intro movie and all in-game cutscene `.snm`/`.znm` files play with audio in sync; skipping works.

### Phase 6: GUI dialogs (4 to 6 weeks, the long pole)

The dialogs are driven by Win32 dialog procedures in `jonesConfig.c` (control configuration, display and sound options, save and load with thumbnails, chapter complete, Indy's trading post, exit confirmation, insert-CD, remap key), `JonesDialog.c` (the frame that draws a dialog over a frozen game frame) and `JonesDevDialog.c` (the developer startup dialog).

Approach: a `j3dgui` library that provides

- A **dialog template table** generated once from `Indy3D.rc` (26 templates, numeric control IDs, positions in dialog units) by a small Python script, so the layouts match the original.
- A **Win32 subset shim** with the same names and semantics used by the existing code: `DialogBoxParamA`, `EndDialog`, `GetDlgItem`, `SendMessageA`/`SendDlgItemMessageA` for the `BM_`, `LB_`, `CB_`, `TBM_`, `EM_`, `STM_` and `WM_` messages that appear in the sources, `SetDlgItemTextA`, `EnableWindow`, `ShowWindow`, `SetFocus`, `MoveWindow`, `GetWindowRect`, `InvalidateRect`. The shim runs a nested modal loop pumping SDL events, exactly as `DialogBoxParam` nested a message loop.
- A **widget renderer** drawing into the back buffer with the engine's `rdFont` and `rdPrimit2`, styled after the original (OpenJones3D's `Resources/mat` holds the Antiqua/Calisto font materials the dialogs used) with HiDPI scaling.
- Replacements for the two native common dialogs (`GetOpenFileName`/`GetSaveFileName`) with an in-game file list, which OpenJones3D already did for the save dialog.
- `JonesDialog.c`'s GDI compositing (`hdcFront`, `hBitmap`) is replaced by drawing the dialog into the back buffer over the captured frame.

Do the developer dialog first (smallest, exercises list boxes and check boxes and gates startup), then exit/confirm, then options, save/load, store, chapter complete, controls.

Milestone: every dialog reachable in a playthrough works; the game can be configured, saved and loaded entirely in-app.

### Phase 7: Full-game verification and 64-bit closure (2 to 3 weeks)

- Scripted level-load smoke test over all `.cnd` files on every commit; a savegame round-trip test per level.
- Full playthrough of all 17 levels plus the bonus level, logging asserts; the decomp compiles the release-kept `*REL` asserts, and the `ninja debug` set can be enabled for extra checking.
- A/B against the Windows build in DREAMM for physics-sensitive puzzles (jumps, whip swings, the Marduk statue push fixed in 1.2) to catch floating-point divergence (see 7.1).
- Remove `win32_shim.h`; no `HWND`, `HDC`, `DWORD` left outside `platform/apple/`.

### Phase 8: Mac product polish (2 to 3 weeks)

- `.app` bundle with Info.plist, icon, Hardened Runtime entitlements, code signing and notarization; `NSHighResolutionCapable`; Game Mode eligibility.
- First-run data locator: detect Steam/GOG/DREAMM installs, otherwise an `NSOpenPanel` for the folder; validate by hashing `INDY3D.EXE` (1.0, 1.2, Steam and GOG hashes are documented in `docs/VERSIONS.md`) and checking the GOB sizes.
- Window/fullscreen toggle, native resolution, vsync, aspect-correct HUD (import OpenJones3D's widescreen and scaling fixes, and the GOG frame limiter equivalents), keyboard/controller glyphs in the control dialog.
- Apply the curated OpenJones3D fix series (crash fixes, COG stack corruption, sound handle restoration, 128 simultaneous sounds, higher mesh and sector limits) behind a build option, defaulting on.
- Crash reporting (write the engine log beside the crash), `--level`, `--windowed`, `--data` command-line flags.

### Phase 9: Stretch

Universal binary, higher-resolution texture loading from loose files, MSAA and anisotropic filtering options, an SDL_GPU backend for Linux/Windows, contributing the portable platform layer back to OpenJones3D.

## 6. Repository layout for the port

```
<port>/
  CMakeLists.txt
  Jones3D/            from the decomp, with Win32 removed
  Libs/{sith,rdroid,std/General,sound,smush/insane,zlib,j3dcore}
  platform/apple/     everything new (Objective-C and C)
  platform/common/    j3dgui, DIK tables, ini settings, headless null backends
  tools/              rc2table.py (dialog templates), gobtool (list/extract GOB), cndcheck
  tests/              level-load, COG compile, savegame round-trip, movie frame checksums
  third_party/        SDL3 (via package or vendored), miniaudio.h
  docs/               PORTING.md (deviations from the decomp), DECISIONS.md
```

Keep the decomp's `docs/` in the repo as the behavioural reference; they document file formats, versions and the known 1.2 changes.

## 7. Risks and how they are handled

### 7.1 Floating-point behaviour

The original ran on x87 with 64-bit internal precision and explicit rounding-mode switches for `floor`/`ceil` (the inline asm in `std.c` and `stdMath.h`). arm64 uses true 32-bit floats and fused multiply-add by default. Physics, whip swings and COG arithmetic may diverge subtly. Mitigations: compile with `-ffp-contract=off`, use the `NONMATCHING_MATH` C fallbacks (`floorf`, `ceilf`, `sqrtf`), keep the engine's own `stdMath_SinCos` table lookups, and A/B the known-sensitive puzzles. OpenJKDF2 shipped the same engine family on arm64 Macs this way without gameplay regressions.

### 7.2 On-disk struct layouts on 64-bit

Covered by the Phase 1 audit. The failure mode is silent (misread levels, corrupt saves), so the level-load and save round-trip tests exist from Phase 1 onward. Savegame compatibility with Windows saves is explicitly out of scope, which frees us to change the in-memory `NdsHeader`.

### 7.3 Dialog system scope

`jonesConfig.c` is 10k lines and the shim must honour exact message semantics (list box selection indices, combo box item data, trackbar ranges). The template table from `Indy3D.rc` and starting with the developer dialog de-risk this early; the game is playable without dialogs from Phase 3, so the schedule risk is contained to Phase 6.

### 7.4 Blocky16 and INSANE correctness

Hand-written assembler with no debug build to read. Mitigation: ScummVM's codec as reference, plus per-frame checksums captured from the Windows build under DREAMM.

### 7.5 Licensing and trademarks

AGPLv3 code, no assets, a project name that avoids LucasArts/Disney marks, and a README stating a retail copy is required. ScummVM-derived code must keep its GPL notices; GPLv2+ is compatible with AGPLv3.

### 7.6 Tooling

SDL3 and miniaudio are stable; Metal and Apple clang are first-party. No deprecated frameworks (OpenGL, OpenAL) are used, so the port is not on a removal clock.

## 8. Milestone summary

| # | Milestone | Rough effort |
| --- | --- | --- |
| 0 | Tree links on arm64 with a null platform | 1 week |
| 1 | Headless boot, all levels load, 64-bit audit done | 2 to 3 weeks |
| 2 | Metal renderer, first level on screen | 3 to 4 weeks |
| 3 | Playable with keyboard, mouse and gamepad | 1 to 2 weeks |
| 4 | Audio complete | 2 weeks |
| 5 | Movies complete | 2 to 3 weeks |
| 6 | All dialogs, save and load in-app | 4 to 6 weeks |
| 7 | Full playthrough verified | 2 to 3 weeks |
| 8 | Signed `.app`, data locator, QoL fixes | 2 to 3 weeks |

Total: roughly 20 to 27 engineer-weeks to a shippable 1.0. Phases 3, 4 and 5 are independent once Phase 2 lands and can overlap if more than one person is available.

## 9. Immediate next steps

1. Create the port repository and import the decomp at its current commit (Phase 0, step 1).
2. Write the CMake skeleton and the `j3dcore` replacement header; get `Libs/std/General` and `Libs/rdroid` compiling clean under clang as the first measure of real portability.
3. Write `tools/gobtool` and extract the level and cog lists from the player's GOBs so the Phase 1 test harness has its inputs.
