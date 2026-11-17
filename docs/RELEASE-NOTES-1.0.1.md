# OpenIndy3D 1.0.1

This update fixes a crash on ledges and makes frame pacing smoother. It also adds Intel Mac support, optional
high-resolution textures, multisampling and anisotropic filtering, and a choice of Indy's outfit.

> **You still need your own copy of the game** (version 1.2, English). OpenIndy3D contains no game data. Saves,
> settings and key sets from 1.0 carry over unchanged.

**Requires:** macOS 13 Ventura or later, on Apple silicon **or an Intel Mac** (new in this release).

---

## 🐛 Fixes

- **Assert while hanging from a ledge.** With Indy hanging from a ledge and the camera pressed against the rock, the
  game could stop with *"sithIntersect.c(976): (\*pSphereHitDist <= moveDistance) …"*. The camera's collision test
  divided two floats whose rounded result could land a hair past the distance it had just checked against. The
  original x87 build never saw this; Apple silicon did. The result is now clamped to that distance.
- **Crash after an assert or debug trap.** After an assert like the one above, the crash handler called itself over
  and over until the stack ran out. The app then died a second time, and `Crash.log` had no backtrace. macOS ignores
  the one-shot handler flag for SIGTRAP and SIGILL, so the handler now resets itself. A crash now writes a full
  backtrace and the engine's last output, and the next launch tells you where to find them.
- **Frame rate locked to 30 fps on a slow frame.** The renderer waited for the GPU to finish all its work before
  starting each scene, so the CPU and GPU took turns. With VSync on, a frame that just missed the refresh waited for
  the next one, and the game fell to half rate whenever the Mac slowed down (Low Power Mode, for example). Three scene
  buffers are now used in turn, so the CPU prepares the next scene while the GPU draws this one.
- **Level 7 saves differed between runs on Intel.** A Cog vector literal with an unreadable component kept leftover
  memory (a bug in the original game). Those components are now zeroed, so saves are byte-identical again on both
  architectures.

## ✨ New

- **Universal app.** One `OpenIndy3D.app` runs natively on Apple silicon and Intel Macs. On Intel GPUs, the Metal
  renderer uses managed textures.
- **High-resolution textures.** Put PNGs in a `hires` folder beside the GOBs (`Resource/hires/<material>.png`, or
  `<material>_cel<n>.png` for one cel of an animated material) and they replace the originals at any size. To start a
  texture set, run with `J3D_HIRES_DUMP=<folder>` to write out every original under its replacement's name.
  *Hires Textures* in Options → Display turns them off.
- **MSAA and anisotropic filtering.** Under Options → Display → *Image Quality*: multisampling at 2, 4 or 8 samples,
  and anisotropic filtering up to 16×. Both are off by default.
- **Indy's outfit.** *Indy's Outfit* in Game Play Options (in game or in the launcher) chooses jacket, no jacket, or
  the game's choice (the default). It applies from the next level start or loaded game, and a saved game follows the
  outfit chosen when it is loaded. On the command line: `--outfit jacket|nojacket|game`.

## 🔧 For developers

- `tools/universal.sh` builds arm64 and x86_64 trees and joins them with `lipo` into `build-universal/OpenIndy3D.app`.
  `--test` runs both test suites, the x86_64 one under Rosetta 2.
- New tests: `render-msaa`, `render-managed`, a PNG round trip in `render`, and `outfit-01_cyn`, `outfit-03_riv`,
  `outfit-17_pru`. All 42 apple tests pass on both architectures, and the layout audit still reports
  0 retail on-disk types that differ.
- Every change to the decompiled source is documented in [docs/PORTING.md](https://github.com/Jenavieve-Rose/OpenIndy3D/blob/v1.0.1/docs/PORTING.md);
  build options are in [docs/BUILDING.md](https://github.com/Jenavieve-Rose/OpenIndy3D/blob/v1.0.1/docs/BUILDING.md).

## ⚠️ Known limitations

- **Not notarized.** The app is signed ad hoc, so macOS asks you to confirm the first launch (right-click → Open).
- **Intel Macs** pass the full test suite under Rosetta 2 but have not yet been played on real Intel hardware.
  Reports are welcome.
- Only the English 1.2 version is tested. Other versions and languages start with a warning.

**Full list of changes:** [v1.0.0…v1.0.1](https://github.com/Jenavieve-Rose/OpenIndy3D/compare/v1.0.0...v1.0.1)

"Indiana Jones", "LucasArts" and Indiana Jones and the Infernal Machine are trademarks of their respective owners.
OpenIndy3D is not affiliated with or endorsed by Lucasfilm, LucasArts or The Walt Disney Company.
