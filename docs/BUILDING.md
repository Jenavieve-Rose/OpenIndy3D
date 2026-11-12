# Building the port

How to build J3DPort on an Apple Silicon Mac, point it at your copy of the game, and run the tests.

The decomp's own build instructions (the byte-matching pipeline, which the port does not use) are kept as
[DECOMP-BUILDING.md](DECOMP-BUILDING.md).

## Requirements

- An Apple Silicon Mac. The build targets arm64 and macOS 13.0 or later.
- Xcode or the Command Line Tools, for Apple clang.
- CMake 3.24 or later, and Ninja: `brew install cmake ninja`.
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

`-DJ3D_TEST_DATA` is optional. Without it everything still builds, but no tests are registered with `ctest`.

| Output | What it is |
| --- | --- |
| `build/jones3d` | The game. |
| `build/levelload`, `build/cogcheck` | The test programs (below). |
| `build/lib*.a` | The engine libraries: `zlib`, `std`, `rdroid`, `sound`, `smush`, `sith`, `platform`, `jones3d_game`. |

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
| `J3D_TEST_DATA` | empty | Data root used by the tests. Registers the tests with `ctest` when set. |
| `J3D_DEBUG_ASSERTS` | `OFF` | Compiles the engine's debug-only `J3D_ASSERT` checks. Off matches the decomp's release build. |
| `J3D_WERROR_SHORTEN` | `OFF` | Makes `-Wshorten-64-to-32` an error. The tree does not build with it yet (PORT-PLAN.md, P1.25 to P1.29). |
| `CMAKE_BUILD_TYPE` | none | `Debug` or `RelWithDebInfo` as usual, for the Ninja generator. |

A build prints several hundred warnings from the imported sources. They are counted and scheduled in PORT-PLAN.md
(phase 1D); pointer-truncating casts are already errors.

## Running

```sh
build/jones3d --data /path/to/data-root
```

| Option | Effect |
| --- | --- |
| `--data <dir>` | The data root. |
| `--headless` | Run with an in-memory display and no window, on a virtual clock (33 ms of game time per frame, not tied to real time). For tests. |
| `--frames <n>` | Quit after n passes of the game loop. Once a level is open a pass is one frame. |

Any other argument is passed to the engine's own command-line parser unchanged.

As of phase 1 there is no display backend: without `--headless` the game opens the GOBs, loads its string tables,
starts the engine, logs to stdout and exits at display open. With it, the game loads the first level and runs:

```sh
build/jones3d --data /path/to/data-root --headless --frames 600
```

A run writes its level-start autosave to `SaveGames/` and any error to `JonesError.txt` under `Install Path`, which
defaults to the data root, that is, into your game folder. Set `J3D_REG_Install_Path=/some/short/folder/` to send
them elsewhere; the folder must exist and the whole path must stay under 128 characters. PORT-PLAN.md tracks what
works.

### Settings and environment variables

Settings are stored in `~/Library/Application Support/J3DPort/settings.ini`, one `<name>=<type>:<value>` per line, using
the value names the retail game kept in the registry. The format is described at the top of
`platform/common/wuRegistry_file.c`.

| Variable | Effect |
| --- | --- |
| `J3D_DATA` | The data root. `--data` sets it. |
| `J3D_SETTINGS` | Path of the settings file to use instead of the default. `/dev/null` runs with defaults and saves nothing. |
| `J3D_REG_<name>` | Overrides one setting for this run; spaces in the name become underscores, for example `J3D_REG_Start_Mode=2`. |
| `J3D_TRACE_FILES` | `1` prints every host file that opens successfully to stderr, which shows the loose files overriding GOB contents. |
| `J3D_HEADLESS` | `1` is `--headless`. |
| `J3D_FRAMES` | `<n>` is `--frames <n>`. |

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
| `boot` | Runs `jones3d --headless --frames 600`: level open, the level-start autosave and 20 s of game time in the first level. Must exit 0 with no assert. Its autosave goes to `build/boot/SaveGames/`, not the game folder. | under 1 s |

Each can be run directly, which shows its output:

```sh
build/levelload /path/to/data-root                  # all levels, with section counts per level
build/levelload /path/to/data-root 00_cyn.cnd       # just the levels named
build/cogcheck  /path/to/data-root
```

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
| `python3 tools/gen_null_platform.py` | Regenerates `platform/common/null/*_null.c` from the public headers of the replaced modules. Run it after changing the script's overrides or its `IMPLEMENTED_ELSEWHERE` set. |

## Before committing

```sh
cmake --build build && ctest --test-dir build --output-on-failure && python3 tools/layout_audit.py
```

Record any change to the imported sources in [PORTING.md](PORTING.md).
