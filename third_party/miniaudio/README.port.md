# miniaudio

Vendored for the port's audio output device (PORT-PLAN.md P4.1, `platform/apple/audio_device_ma.c`).

| | |
| --- | --- |
| Version | 0.11.25 (released 2026-03-03) |
| Source | `https://raw.githubusercontent.com/mackron/miniaudio/0.11.25/miniaudio.h` and `LICENSE` from the same tag |
| SHA-256 `miniaudio.h` | `ac7af4de748b7e26b777f37e01cee313a308a7296a3eb080e2906b320cc55c89` |
| SHA-256 `LICENSE` | `457f1b500e0adf6bc059edddfa78a2f62012e7c3bb43476c20e0bd23b25ba0eb` |
| License | Public domain (Unlicense) or MIT No Attribution, at the user's choice (`LICENSE`) |

`miniaudio.h` and `LICENSE` are unmodified. `miniaudio.c` is the port's one-line implementation file; the
configuration switches (device only, Core Audio only) are the `miniaudio` target's compile definitions in
`CMakeLists.txt`. It is compiled without the project's warning flags.

To update: fetch both files from the new tag, check the hashes into this table, rebuild, and run the tests.
