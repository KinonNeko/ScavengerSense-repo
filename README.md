# Scavenger Sense

An SKSE plugin for Skyrim Special Edition / Anniversary Edition. Press a key and
everything worth looting lights up in an outward wave, with names over the
objects and the colour draining from the world while it runs. It reads people
too — relationships, titles, and who is a merchant.

**Nexus page:** <https://www.nexusmods.com/skyrimspecialedition/mods/188380>
**Author:** KShakes — <https://www.nexusmods.com/profile/KShakes>

This repository is the source. Release builds are attached to the Nexus page as
a FOMOD; you do not need to build anything to play the mod.

## Licence

**GPL-3.0-or-later.** See [LICENSE](LICENSE).

This plugin statically links [CommonLibSSE-NG][cl], which is GPL-3.0-or-later.
A plugin that links it forms a combined work and must itself be GPL-3.0-or-later
or GPL-compatible, so that is what this is. Publishing this repository is how the
Corresponding Source requirement is met — anyone who has the DLL can get the code
that built it.

CommonLibSSE-NG's Modding Exception covers linking against Skyrim itself and
against proprietary hardware SDKs. It does **not** extend to plugin code.

Other dependencies, all GPL-compatible:

| Component | Licence |
|---|---|
| CommonLibSSE-NG | GPL-3.0-or-later (with Modding Exception) |
| cimgui / Dear ImGui | MIT |
| SKSE Menu Framework + SDK | MIT |
| spdlog | MIT |
| DirectXTK | MIT |
| rapidcsv | BSD-3-Clause |
| Noto Sans SC (`mod/SKSE/Plugins/fonts/`) | SIL Open Font License 1.1 — see `OFL.txt` beside it |

## Layout

```
plugin/src/          the plugin itself
plugin/CMakeLists.txt
tools/make_esp.py         builds ScavengerSense.esp from scratch (128 EFSH records)
tools/build_fomod.py      generates the FOMOD payload tree and ModuleConfig.xml
tools/validate_fomod.py   checks the FOMOD before packaging
mod/                 the data files that ship: marker rules, title rules,
                     translations, icons, presets, the bundled font
fomod/info.xml       FOMOD metadata (ModuleConfig.xml is generated)
```

Everything under `mod/SKSE/Plugins/*.ini` is plain text and is meant to be
edited — the marker rules, the title rules and the translations are data, not
code.

## Building

Cross-compiled from Linux with clang-cl, or built on Windows with MSVC. The
CMake project expects the dependencies as sibling directories at the repository
root, which is what the submodules provide.

```sh
git clone --recursive https://github.com/<you>/ScavengerSense
cd ScavengerSense
cmake -S plugin -B plugin/build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -Ddirectxtk_DIR=$PWD/cmake-pkgs/directxtk
cmake --build plugin/build
```

If you already cloned without `--recursive`:

```sh
git submodule update --init --recursive
```

Cross-compiling from Linux additionally needs an MSVC sysroot; pass
`-DCMAKE_TOOLCHAIN_FILE=msvc-toolchain.cmake`.

`extern/lib/` holds the SKSE Menu Framework import library and its `.def`. The
framework re-exports the cimgui C API and is linked statically, so a missing or
too-old SKSE Menu Framework means the plugin fails to load rather than
degrading — 3.13 or newer is required.

### Producing a release

```sh
python3 tools/make_esp.py        # writes ScavengerSense.esp
python3 tools/build_fomod.py     # writes fomod-build/
python3 tools/validate_fomod.py  # checks it
```

`validate_fomod.py` verifies the ModuleConfig against the FOMOD 5.0 schema
ordering, that every payload folder is referenced exactly once, that no two
simultaneously-selectable options write the same file, and — usefully — that
every key in every installer fragment is a setting `Config.cpp` actually reads.

## How settings load

Precedence, lowest first:

1. Built-in defaults in `Config.h`.
2. `Data/SKSE/Plugins/ScavengerSense_setup/*.ini`, in filename order. These are
   the installer's answers; each holds only the keys its answer changes.
3. `Data/SKSE/Plugins/ScavengerSense.ini`, the player's own file, which the menu
   writes in full — so once it exists the fragments no longer matter.

A preset loaded from the menu deliberately skips step 2, because a preset is
meant to describe a whole look on its own.

## Contributing

Bug reports are most useful with `ScavengerSense.log` attached. It is written to
`Documents\My Games\Skyrim Special Edition\SKSE\`, with a second copy at
`Data/SKSE/Plugins/ScavengerSense-diag.log` — under Mod Organizer that lands in
`overwrite`, which is easier to reach. Set `debug = true` under `[General]` for
per-object detail.
