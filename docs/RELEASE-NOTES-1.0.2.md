# OpenIndy3D 1.0.2

A bug-fix update. 3D sound now comes from the right direction, and a controller's triggers can be bound to actions.

> **You still need your own copy of the game** (version 1.2, English). OpenIndy3D contains no game data. Saves,
> settings and key sets from 1.0 and 1.0.1 carry over unchanged.

**Requires:** macOS 13 Ventura or later, on Apple silicon or an Intel Mac.

---

## 🐛 Fixes

- **3D sound came from behind you.** With *Sound 3D* on, a sound in front of Indy was heard behind, and left and
  right were swapped. The game hands the sound system the camera's forward direction already reversed, and then
  reverses it again for DirectSound. The port undid only the second reversal, so the listener faced away from the
  camera. The listener now faces the way the camera looks, with or without *Reverse sound*. With *Sound 3D* off (the
  default) nothing changes.
- **Controller triggers could not be bound.** LT/RT (L2/R2) were reported only as one combined axis, which the
  bind dialog never picks up, so they could not be mapped to anything. A gamepad now also reports them as buttons
  (pressed past a third of their travel), and the controls menu shows them with the LT/RT symbols. The combined
  axis is unchanged, so existing key sets work as before.

## 🔧 For developers

- The `sounddriver` test now feeds the listener what `sithSoundMixer_Update` and `Sound_Update` actually pass.
- [docs/PORTING.md](https://github.com/Jenavieve-Rose/OpenIndy3D/blob/v1.0.2/docs/PORTING.md) corrects its account
  of retail's 3D listener: it faced the camera's way, with its top flipped to suit DirectSound's left-handed axes.

## ⚠️ Known limitations

- **Not notarized.** The app is signed ad hoc, so macOS asks you to confirm the first launch (right-click → Open).
- **Intel Macs** pass the full test suite under Rosetta 2 but have not yet been played on real Intel hardware.
  Reports are welcome.
- Only the English 1.2 version is tested. Other versions and languages start with a warning.

**Full list of changes:** [v1.0.1…v1.0.2](https://github.com/Jenavieve-Rose/OpenIndy3D/compare/v1.0.1...v1.0.2)

"Indiana Jones", "LucasArts" and Indiana Jones and the Infernal Machine are trademarks of their respective owners.
OpenIndy3D is not affiliated with or endorsed by Lucasfilm, LucasArts or The Walt Disney Company.
