# OpenJones3D's fixes in the port

The triage of [OpenJones3D](https://github.com/smlu/OpenJones3D)'s `CHANGELOG.md` (v0.1 to v0.3.1, upstream
`a675b39`) for the port (PORT-PLAN.md P8.8, P8.9). OpenJones3D is a reimplementation of the same engine; many of its
entries are its own work (modules implemented, its own transcription slips corrected) and only some fix bugs that are
in the retail game, and so in the decomp the port is built from.

Every fix applied is in the source under `#ifdef J3D_FIXES`, marked `Fix (OpenJones3D <commit>)` (or `v0.1` for the
entries of that release, which predate per-fix commits). `J3D_FIXES` is a CMake option, on by default; with it off the
build keeps retail's behaviour, for comparisons with the Windows build. Undefined behaviour that a fix also removes is
given a defined value in both builds (decision 18), marked `Port:`.

Status: **applied**; **already right** (the decomp has retail's code, which was correct; the entry corrects
OpenJones3D's own transcription); **port already** (the port fixed it earlier, under `J3D_PORT`); **n/a** (code the
port replaces: DirectDraw, Direct3D, GDI dialogs, the Windows console and registry, the Win32 kernel); **not applied**
with the reason; **open** (left for later, with the reason).

## Crashes and memory errors

| Entry | Commit | Status |
| --- | --- | --- |
| `stdMath_NormalizeAngle` float precision: angles just under a multiple of 360 came out negative, and `stdMath_SinCos` read before its table (the inventory crash); NaN check | v0.1 | applied |
| `sithVoice_AddSubtitle` endless loop on a word too long for a line; subtitle table bound; copies bounded and terminated | v0.1 | applied |
| `sithVoice_Draw`: index past the subtitle table when every line has expired | v0.1 | applied |
| `sithVoice_PlayVoice`: no return after a bad thing (null read, two results pushed) | e9d4167 | applied |
| `stdStrTable_ReadLine` endless loop at the end of a file with no final line break | 6b71793 | applied |
| `stdConffile_ReadArgsFromStr`: a 513th argument written past the table | v0.2 | applied |
| `sithControl_UnbindFunction` cleared 8 bytes, not the bindings; `RegisterControlCallback` bound off by one | v0.3 | applied |
| `sithWhip_Reset` through a closed camera (quitting while aiming the whip) | fea0e59 | applied (in `sithCamera_RestoreExtCamera`) |
| `sithWeapon` aim with no local player (same quit) | v0.1 | applied |
| `sithWeapon` auto-aim: an empty target slot | v0.3 | applied |
| `sithThing_ScaleCombatDamage`: an explosion read the weapon info it does not have | v0.1 | applied (defined in both builds) |
| `sithAnimate_BuildScrollFaceRotate` endless loop: `dvert` not recalculated | 70cf7bf | applied |
| `sithAnimate` push and pull completion with the item gone | 70cf7bf | applied |
| `sithAnimate_UpdatePushItemAnim`, `PullItemAnim`: the final move along an unset vector | 70cf7bf | port already (`direction3`, Phase 1) |
| `sithAnimate_StartSpriteSizeAnim`, `StartThingMove`: division by a zero time, an animation that never ends | f5dc718 | applied |
| `sithAnimate_UpdateSpriteSizeAnim` end test on the value instead of the delta | f5dc718 | already right |
| `sithDSSThing` attachment to a face of a model's other mesh: an index past the face table on restore | 45c8065 | applied |
| `rdPuppet` track reset with more model nodes than the track keeps (write past the table); null puppet reads | v0.1 | applied (the bound); the null reads follow asserts |
| `JonesFile_FilePrintf` passed the formatted text as a format | v0.1 | applied |
| `JonesFile` path buffer 48 characters | v0.1 | port already (`J3D_PATHSIZE`) |
| `JonesMain` level number 18 from the command line, past the level table | 24ab38b | applied |
| `JonesHud` selected and changed item spin angle unbounded (the item never comes to rest; the menu could hang) | v0.1 | applied |
| `JonesHud` credits: an index read before its bounds were tested | v0.1 | applied |
| `JonesHud_StartItemTranslation` division by a zero move rate | v0.1 | applied |
| `JonesHud_MenuClose` move rate unset | v0.1 | port already (Phase 2 audit) |
| `sithSoundMixer_PlaySoundPos` environment flags, `sithWeapon_FireProjectile` torso muzzle offset unset | v0.1 | port already (decision 18) |
| `wkernel_Run` endless loop on an unexpected process result; exit code | v0.1 | n/a (`wkernel_sdl.c`) |
| `stdBmp_Load` HDC deleted on error; `stdColor_ColorConvertOneRow` 24-bit rows | v0.1, 9e6ba77 | n/a (GDI; the port converts its own) |
| Restoring the back buffer on a lost surface | v0.1 | n/a (DirectDraw) |
| Sound compression clipping; `std3D_AllocSystemTexture` 16-bit rows | v0.1 | n/a (the port's mixer and Metal textures) |
| Keyframe joint count bound | v0.1 | applied with the `rdPuppet` bound above |

## The COG stack and sound handles

| Entry | Commit | Status |
| --- | --- | --- |
| COG thing functions that pushed no result on an error (`GetThingRespawn`, `SetThingMesh`, `GetThingMass`, `GetThingMaxVel`, `GetThingMaxAngVel`, `GetJointAngle`, `SetMaxHeadPitch`, `SetMinHeadPitch`, `SetMaxHeadYaw`), one that pushed two (`FirstThingInSector`), and two that went on with a null thing (`GetThingAlpha`, `IsThingCrouching`): the stack is unbalanced and the script reads a wrong value | e9d4167 | applied |
| `IsModePlaying`, `PauseMode`, `ResumeMode`: a mode number past the table, a thing with no puppet | e9d4167 | applied |
| `SetJointAngle`, `GetJointAngle`: a joint number past the table | e9d4167 | applied |
| `ThingLight` with no lit-up time left the range out of the colour's alpha (light flicker near the camera) | e9d4167 | applied |
| COG stack and call stack bound `==` to `>=` | e9d4167 | applied |
| The other COG function entries in e9d4167 (vector checks, log text, an extra null check before an assert) | e9d4167 | not applied: the vector a failed pop leaves is zero in the decomp, and the rest is messages |
| Float to int casts on the COG stack (`rintf`) | 0ada120 | already right (retail truncates, as the decomp does) |
| `CreateLaser` with a bad end position | e9d4167 | already right (the 1.2 change, docs/VERSIONS.md) |
| Restored sound handles 1234 higher than saved (scripts lose their sounds); handle collisions after a restore | 6c3acc4 | applied (the `saveload` tests expect equal handles with the fixes) |
| Playing a far sound on restore | b8a82d4 | open: the restore's far listener is kept, and the round trip test covers what it changes |
| `sithPlayer_Close` left IMP fire, invisibility, jewel flying and the swimming inventory to the next level | 645471a | applied |
| `sithPlayer_Update`: an overheated IMP part stopped hurting above 40 frames a second | v0.1 | applied (the port runs at the display's rate, 120 Hz here) |
| `sithWhip` climbing down with jump-off pressed: both moves started, the controls could lock | v0.1 | applied |
| Dead thing on lava | 18d828d | applied |
| Path mover blocked: time step | f361b65 | already right |
| `sithControl` joystick option masks (axis flags in the id) | v0.3 | not applied: the port's controller layer (`stdControl_sdl.c`) gives plain ids |
| `stdControl_ReadAxisAsKey` key state | 1ab4c44 | n/a (`stdControl_sdl.c`) |
| Savegame file handle | a90a59d | n/a (the port's file layer) |
| `sithModel_GetModelByIndexWrap` index type | 669904d | n/a (OpenJones3D's own wrapper) |

## Raised limits

| Entry | Commit | Status |
| --- | --- | --- |
| Sounds playing at once, 32 to 128 | d8fdedc | applied |
| Visible sectors, 128 to 4096 | 08a033bf, b448041 | applied (`SITHRENDER_MAX_VISIBLE_SECTORS`) |
| `rdCache` vertex buffer, `rdModel3` mesh limits | 25ceaba, a008386 | not applied: retail data stays well inside them (the cache flushes every 128 faces of at most 64 vertices, 8,192, the size of its intensity table); they matter for high-detail models, which the port does not load |
| Display resolutions over 2048 | 6f9db7e | n/a (Metal presents at any size) |
| 32-bit textures by default; 24 and 32-bit textures in CND files | 249a416, 3152496, 91d0a87 | n/a (the Metal renderer converts every texture to 32 bits) |

## Quality of life (behaviour changes)

Not applied. They change how the game plays rather than fix a fault, and OpenJones3D keeps them behind its own
`J3D_QOL_IMPROVEMENTS`. Each could be offered as an option later.

| Entry | Commit |
| --- | --- |
| Looping far sounds kept when out of earshot, stopped below the far threshold; the loop flag removed from sound class modes | fddeae4, 2a25add, 5a169d7, 2a757af |
| AI at cliff edges; AI head-on collisions on paths | 8ed5690, 49ef284 |
| Fall speed kept through a jump-rollback | c1673ac |
| `AnimateSpriteSize` returns -1 for an instant change | 039a227 |
| The 800x600 intro movie by default; HD models by default | ffa2e9d, v0.1 |

## Widescreen and the HUD

| Entry | Commit | Status |
| --- | --- | --- |
| Camera focal length by the canvas's shape: on a wide picture retail showed the 4:3 width and cut the height | 972acb3f | applied, for pictures more than 1% wider than 4:3 only; a narrower one keeps retail's width (OpenJones3D scales both ways, which cuts a portrait picture's sides) |
| Health and endurance indicators stretched with the screen's shape | v0.1 | applied, with the smaller of the two scales (OpenJones3D takes the width's, which enlarges them on wide screens) |
| The inventory-changed item's place at the right | v0.1 | applied |
| Inventory menu position and scale on wide screens | v0.1, 4902ea35 | not needed: with the focal length fixed the row sits as at 4:3, its items in proportion (checked at 1920x1080); OpenJones3D's scale and Z changes answer its own camera code |
| Projection with the camera's aspect ratio (`OrthoProject`, `PerspProject`, `rdClip`, the sky's horizon scale) | 1e86d451, 1f2208c2 | not applied: the aspect ratio is 1 but in the underwater wobble (`sithRender`), and the Z changes correct OpenJones3D's own transcription |
| Ceiling sky texture offset; sprite orientation | 2a200d5, 4142482c | already right (OpenJones3D's own transcription) |
| End credits at other resolutions; the credits music fade | v0.1, 3d9ba287 | not applied: the credits are centred on the canvas and their icons a fixed 64-pixel square, so the shape does not distort them; the larger icons at high resolution are a change of look |
| Fading plate models at 21:9 | 5d705fb, bf2c4f7 | n/a (OpenJones3D's own models, not in the retail data) |

Checked with `J3D_HEADLESS_SIZE=1920x1080` shots of all 17 levels, the inventory and the map; at 4:3 sizes every change
is a no-op by construction (the display test pins the focal lengths).

## Not fixes

- Modules implemented, refactored or renamed (`sithCogFunction`, `sithAI`, `sithPuppet`, `Sound`, `SoundDriver`,
  `sithWorld`, `sithGamesave` and the rest), log functions and messages, typos, enum names, `STRICT`, intellisense:
  OpenJones3D's own work. The decomp already has these functions.
- Developer console commands and COG debug functions (`noclip`, `jumplevel`, `DebugPrint` and the rest), the editor
  mode's mouse buttons and sensitivity, `byacc` and `flex`: tools, not fixes.
- The GDI dialogs (save, load, exit, chapter completed, store, key configuration: sizes, fonts, icons, thumbnails,
  DPI scaling): n/a, the port's dialogs are SwiftUI and its own dialog layer (Phase 6).
- `JonesError.txt` in the working folder, the resource path in the VFS, the developer dialog's level search: the port
  has its own folders (`host_dirs.h`).
- 2D line drawing in 24 and 32-bit buffers, horizontal wall lines, line vertices (`rdPrimit2`, the overlay map):
  open, to check against the port's overlay map, which draws through `std3D_metal.c`.
