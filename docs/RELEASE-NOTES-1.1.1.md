# OpenIndy3D 1.1.1

A small update. The IQ picture is the right way up again, the Mac's Fn and F13 keys no longer reach the game, and
Home takes a screenshot.

> **You still need your own copy of the game** (version 1.2, English). OpenIndy3D contains no game data. Saves,
> settings and key sets from 1.1 carry over unchanged.

**Requires:** macOS 13 Ventura or later, on Apple silicon or an Intel Mac.

---

## 🐛 Fixes

- **The IQ picture was upside down.** On the Game Statistics and Level Completed screens, the IQ picture with your
  points was drawn flipped. The game keeps that picture bottom row first, as Windows bitmap files are stored, and the
  Mac menus read it top row first. The menus now read every picture in its own row order. Save game thumbnails look
  the same as before.
- **The Fn key reached the game.** Some keyboards send Fn (🌐) as an ordinary key, which the game saw as Right
  Command. Fn on its own is now ignored. The keys Fn makes with others still work as usual (Fn+Left Arrow is Home, for
  example).
- **F13 reached the game.** F13 was seen as Print Screen. It is now ignored, and the Controls menu no longer accepts
  it as a binding.

## ✨ Changes

- **Home takes a screenshot**, as F12 does, in the game and during the intro. Mac keyboards have no Print Screen key,
  and macOS often uses F12 for itself. On a laptop, Fn+Left Arrow sends Home. Screenshots go to
  `~/Library/Application Support/OpenIndy3D/ScreenShots`, as before.

## 🔧 For developers

- `GetObjectA` in the GDI shim now fills a `DIBSECTION` when it is given room for one, as Windows does. Its
  `dsBmih.biHeight` is negative for a top-down bitmap.
- New `J3DControl_IsIgnoredKey` in `stdControl_sdl.h` names the keys the game never sees.
- [docs/PORTING.md](https://github.com/Jenavieve-Rose/OpenIndy3D/blob/v1.1.1/docs/PORTING.md) ("After 1.1")
  describes the changes.

## ⚠️ Known limitations

- **Not notarized.** The app is signed ad hoc, so macOS asks you to confirm the first launch (right-click → Open).
- **No disk image with this release.** Build the app from source (docs/BUILDING.md).
- **Intel Macs** pass the full test suite under Rosetta 2 but have not yet been played on real Intel hardware.
  Reports are welcome.
- Only the English 1.2 version is tested. Other versions and languages start with a warning.

**Full list of changes:** [v1.1.0…v1.1.1](https://github.com/Jenavieve-Rose/OpenIndy3D/compare/v1.1.0...v1.1.1)

"Indiana Jones", "LucasArts" and Indiana Jones and the Infernal Machine are trademarks of their respective owners.
OpenIndy3D is not affiliated with or endorsed by Lucasfilm, LucasArts or The Walt Disney Company.
