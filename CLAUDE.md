# Scavenger Sense — working notes

Context for whoever picks this up, human or otherwise. Written at the end of the
session that built 0.5 beta and the vitals work on top of it.

## What it is

An SKSE plugin. Press a key, everything worth looting lights up in an outward
wave, names float over it, the colour drains out of the world. It reads people
too: relationships, titles, and vitals bars.

Nexus: https://www.nexusmods.com/skyrimspecialedition/mods/188380
Published version: **0.5 beta**. Everything after that is unreleased.

## Build

Two routes. Windows is the normal one:

```powershell
cmake -S plugin -B plugin/build -G "Visual Studio 17 2022" -A x64 `
      -Ddirectxtk_DIR="$PWD/cmake-pkgs/directxtk"
cmake --build plugin/build --config Release
```

Cross-compiling from Linux (what the original session used) additionally wants
`-DCMAKE_TOOLCHAIN_FILE=msvc-toolchain.cmake` and an MSVC sysroot.

The first build compiles CommonLibSSE-NG — 363 targets, slow. After that a
source change is seconds. **Do not delete `plugin/build`** to "start clean"
without a reason; reconfiguring in place keeps the object cache.

`git clone --recursive`, or `git submodule update --init --recursive`. The
CMake project expects the dependencies as siblings at the repo root, which is
what the submodules give you.

## Where things live

| File | What it owns |
|---|---|
| `Main.cpp` | plugin declaration, key handling, message hooks |
| `Sense.cpp` | the sweep: candidate scan, shader pool, per-frame tick |
| `Labels.cpp` | everything drawn on the finished frame — tags, ring, vignette, bars |
| `Config.cpp/.h` | every setting, its INI key, its clamp, its comment |
| `Marks.cpp` | marker rules from `ScavengerSense_marks*.ini` |
| `Titles.cpp` | title rules from `ScavengerSense_titles.ini` |
| `Relations.cpp` | the only code that writes to the save |
| `PostFX.cpp` | the Community Shaders route, off by default |
| `GameMenus.cpp` | menu awareness, and hiding the game's interface |

## Things that will bite you

- **SKSE Menu Framework 3.13+ is linked, not looked up.** Missing or too old and
  the DLL does not load at all. Note 3.13 is newer than the version shown on
  SMF's own Nexus page.
- **The esp is not optional.** The 128 effect shaders are records in it and
  cannot be made at runtime. ESL-flagged. Built by `tools/make_esp.py`, not by
  the Creation Kit — edit the script, not the file.
- **Adding a setting means five places**: the member in `Config.h`, the `Get` in
  `LoadFrom`, a clamp, the writer in `Save`, and the menu control. Miss the
  writer and it silently resets on save.
- **Every menu string needs a translation.** `mod/SKSE/Plugins/ScavengerSense_chinese.ini`
  is keyed by the exact English text, with `\n` kept as two characters and any
  `=` in the key escaped as `\=`. There is a checker in the session history; it
  is worth rewriting as a script if you touch the menu much.
- **`Labels::Entry` is aggregate-initialised positionally** in `Sense.cpp`. Add
  fields at the END or the initialiser silently shifts.
- **Two threads.** The main thread owns game data; the render thread only reads
  what was handed to it under `Labels::_lock`. Never touch a `TESForm` from
  `Render()`.
- **Scaleform menu names are case sensitive**, and the string `Get` overload
  lower-cases. `hideMenus` is read through `Lookup` directly for that reason.

## Settings precedence

1. Built-in defaults in `Config.h` — these are the tuned Oathvein values.
2. `SKSE/Plugins/ScavengerSense_setup/*.ini`, filename order — installer answers.
3. `SKSE/Plugins/ScavengerSense.ini` — the player's own, written whole by the menu.

A preset loaded from the menu deliberately skips step 2.

## Releasing

After a build, `tools/deploy.ps1` copies the DLL, the esp and the data INIs
into the live MO2 install. It merges rather than overwrites the `[Assigned]`
block of the titles file, so titles typed in game survive a deploy, and it
never touches `ScavengerSense.ini` — that belongs to the game, not the repo.

```sh
python3 tools/make_esp.py
python3 tools/build_fomod.py
python3 tools/validate_fomod.py   # run this, it catches real mistakes
```

`validate_fomod.py` checks schema ordering, that every payload folder is
referenced exactly once, that no two simultaneously-selectable options write the
same file, and that every key in every installer fragment is a setting
`Config.cpp` actually reads.

## Habits worth keeping

The bugs found in this project were found by **measuring, not looking**:

- Bar geometry was verified by computing quad areas against length × thickness.
  That caught upright bars rendering at nearly double width — invisible in a
  screenshot, obvious in a number.
- Corner placement was verified by checking all 12 positions land on screen.
- The font was verified by measuring ink coverage, which disproved a "the font
  is too thin" assumption that a visual comparison had seemed to confirm.

When something is geometric or numeric, write the four-line script.

## Licence

GPL-3.0-or-later, because CommonLibSSE-NG is and this links it statically. The
Modding Exception covers linking against Skyrim, not plugin code. Publishing
this repository is how the Corresponding Source obligation is met — keep the
link on the Nexus page.

MIT/BSD notices for spdlog, DirectXTK and rapidcsv ship in `mod/README.md`.
Dear ImGui is deliberately absent from those: headers only, no object code.
