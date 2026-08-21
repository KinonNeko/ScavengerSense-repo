# Changelog

Every published version, newest first. Versions are the ones that reached
Nexus; the numbering skips where a build was cut but never shipped.

The version lives in three places that must agree, and a release is not a
release until they do: `REL::Version` in `plugin/src/Main.cpp`,
`MachineVersion` in `fomod/info.xml`, and `project(... VERSION)` in
`plugin/CMakeLists.txt`.

## 0.8.8 beta

- The ammunition readout ships off. It was the one always-on readout in
  the mod that defaulted to on, against the comment sitting directly
  above it, and against the installer's own preset - so a new player saw
  a count floating by their bow that choosing the author's answer then
  took away. Turn it on from the Ammo page.
- 0.8.7 was cut and tagged but never published; it is this build without
  the line above.

## 0.8.7 beta

Spent ore veins and emptied ash piles, properly this time. Both switches had
shipped broken, and both were fixed by measuring rather than guessing.

- **Emptied ash piles now go dark.** The switch had never once run: the code
  chose between the ash test and the vein test by asking whether the reference
  carried inventory changes, and an emptied pile carries none, so every pile
  fell through to the vein branch. Underneath that sat a second problem — a
  pile keeps none of the loot. Its own readings (inventory, container, linked
  reference) are identical before and after looting, and identical to a dungeon
  lever's. The body that burned is still there holding everything, and the two
  carry each other's handle, so the pile is now judged by asking the body.
- **Unmined veins are no longer hidden.** The old test was
  `IsActivationBlocked()`, which is already true for a vein nobody has touched.
  Mining moves one undocumented bit, measured against three untouched veins as
  a control, and that is what the switch reads now.
- **Ore vein names no longer hang a storey overhead.** Tag height came from the
  3D bounding *sphere*, whose radius describes how wide a thing is, not how
  tall. An iron vein is 278 units across and barely rises off the rock, which
  put its name 175 units up. The editor bounding box knows the difference and
  now caps the lift — 72 units for the same vein. It can only lower a tag,
  never raise one, because a rotated mesh makes the box's own height a guess.
- The two switches are independent questions again; one no longer sits inside
  the other's negative branch.

## 0.8.5 beta

- Fixed the crash when the console was opened over an NPC mid-sweep. The cause
  was `Actor::IsInFaction`, a virtual call into the game, reached from the
  title and marker rules while the console held the frame. Both now walk the
  actor's factions instead.
- Menu text no longer arrives as mojibake on systems running the Windows
  "Use Unicode UTF-8" beta option. Where the ANSI code page is UTF-8 but the
  locale's legacy page is not, game text that fails UTF-8 validation is read
  through the legacy page instead. Every other machine is byte-identical.
- The Keys page was rebuilt: the all-in-one choice sits at the top and the
  per-action controls follow from it, so a key set to "single press" is a
  single press.
- The sweep chime record was authored without the plugin's own mod index, so
  it never resolved and never played.

## 0.8.2 beta

- The shipped defaults are the "Only sensing" answer; the Default preset is
  gone.
- The chime is off out of the box. The ammunition readout is on, and appears
  only while a bow or crossbow is drawn.
- The installer's presets are loadable from the menu.

## 0.8.1 beta

- An ammunition readout: on the character, over the head, or fixed to a screen
  corner, with pixel sliders and its own fade.
- Switches to hide spent ore veins and emptied ash piles. (Both were wrong;
  see 0.8.7.)
- The installer warns about the HUD before KShakes's Choice is taken.

## 0.8 beta

- Four ways in, one of them the author's own; Y carries everything out of the
  box.
- Containers are judged by what a player would actually find, so a looted
  chest stops reading as full.
- The placeholder filter speaks Chinese and was born knowing the phrase.

## 0.7.2 beta

- Tracking: the crouch becomes the hunt, one reveal rule, honest footprints.
- Auto-capture watches the world rather than the door.
- The installer learns to track.

## 0.7 beta

- Vitals bars: per-person lifetimes, their own switches, placement by drag,
  and the ceiling a survival mod has taken away.
- Titles can be copied, assigned, and written in the menu.
- Every menu string is translated, with a checker to keep it that way.

## 0.5 beta

First published build.
