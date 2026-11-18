# OpenIndy3D 1.1

An update that makes your settings and controls stay put, turns on head tracking for 3D sound, and gives the game a
copy of its data in its own folder.

> **You still need your own copy of the game** (version 1.2, English). OpenIndy3D contains no game data. Saves and
> settings from 1.0.x carry over. Key sets saved with 1.0.x need their controller buttons bound again (see below).

**Requires:** macOS 13 Ventura or later, on Apple silicon or an Intel Mac.

---

## 🐛 Fixes

- **Game play settings did not stay.** The launcher's Options window read Difficulty, Show Map Hints, Map Rotation,
  Default to Run and Show Text before the game had loaded them. It showed the starting defaults, and pressing OK
  saved those over your choices. Options now shows and keeps what you saved.
- **Controller key sets did not stay.** Saving a key set wrote an empty entry in place of every other binding and
  dropped the rest, so most controller buttons were lost. A set saved that way then failed to load, and the game went
  back to the default set. Key sets are now saved and loaded back exactly.
  - Sets saved with 1.0 to 1.0.2 load again with the bindings that were kept, mostly the keyboard keys. Their
    controller buttons were never written to the file, so bind them again once and choose the set in Controls.
- **The chosen key set was replaced.** When the chosen set was the built-in *Dual-Stick Controller*, pressing OK in
  Options or Controls saved the first set in the list in its place. The chosen set now stays unless you pick another.
- **Head tracking was off.** 3D sound's head tracking had to be turned on by hand in the settings file and nothing
  showed it. It is now on by default (macOS 15 and later, with headphones that track your head, such as AirPods Pro),
  and the Sound page has a *Head Tracking* box that takes effect at once. The 1.0.2 listener fix did not change it.

## ✨ Changes

- **The game keeps its own copy of the game data.** On first launch it copies what it needs to run (`Resource`,
  `KeySets`, and `Jones3D.exe` or `Install/Launch.bmp` for the launcher picture) from your Steam, GOG or DREAMM folder
  into `~/Library/Application Support/OpenIndy3D/GameData`. It checks the copy, then runs from it. Your store's folder
  is left as it is, so the store never sees missing files. On the same disk the files are APFS clones: the copy is
  instant and takes no extra space. On another disk it is a full copy of about 860 MB.
  - Set `J3D_NO_DATA_COPY=1` to run from the store's folder instead. A folder given with `--data` is never copied.

## 🔧 For developers

- `jonesConfig_ControlOptions_WriteScheme` read a set's rows through retail's `int (*)[9]`, which matched its 32-bit
  `size_t`. The rows are now read as `size_t`, and the loader skips empty names.
- New `J3DAudio_HeadTrackingAvailable` and `J3DAudio_SetHeadTracking` in `host_audio.h`; the Sound menu's settings
  carry `bHeadTracking`.
- `hostdata_MakeLocal` makes the copy through `GameData.partial` and checks it with `J3DHost_CheckData` before it
  replaces an earlier copy. `Data Source` in `settings.ini` records where it came from.
- [docs/PORTING.md](https://github.com/Jenavieve-Rose/OpenIndy3D/blob/v1.1.0/docs/PORTING.md) ("After 1.0.2") and
  [docs/BUILDING.md](https://github.com/Jenavieve-Rose/OpenIndy3D/blob/v1.1.0/docs/BUILDING.md) describe the changes.

## ⚠️ Known limitations

- **Not notarized.** The app is signed ad hoc, so macOS asks you to confirm the first launch (right-click → Open).
- **No disk image with this release.** Build the app from source (docs/BUILDING.md).
- **Intel Macs** pass the full test suite under Rosetta 2 but have not yet been played on real Intel hardware.
  Reports are welcome.
- Only the English 1.2 version is tested. Other versions and languages start with a warning.

**Full list of changes:** [v1.0.2…v1.1.0](https://github.com/Jenavieve-Rose/OpenIndy3D/compare/v1.0.2...v1.1.0)

"Indiana Jones", "LucasArts" and Indiana Jones and the Infernal Machine are trademarks of their respective owners.
OpenIndy3D is not affiliated with or endorsed by Lucasfilm, LucasArts or The Walt Disney Company.
