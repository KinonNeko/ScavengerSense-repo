# Scavenger Sense — working notes

Context for whoever picks this up, human or otherwise. Grown over the sessions
that built 0.5 through 0.8 beta.

## What it is

An SKSE plugin. Press a key, everything worth looting lights up in an outward
wave, names float over it, the colour drains out of the world. It reads people
too: relationships, titles, and vitals bars. Since 0.7.2 it also tracks them —
marked quarry leave trails you can follow.

Nexus:  https://www.nexusmods.com/skyrimspecialedition/mods/188380
Source: https://github.com/KinonNeko/ScavengerSense-repo

**The repo is at 0.8 beta** — `plugin/src/Main.cpp` (`REL::Version`, and the
load banner) and `fomod/info.xml` are the three places that carry the number,
and they must agree.

This file no longer tries to say which version is *published*. It carried
"0.5 is published, the repo is at 0.6" for three releases after that stopped
being true, because a hand-maintained copy of state that lives somewhere else
drifts silently. The Nexus page is the authority; ask it, not this file.

What is still worth recording is the shape of the risk: **releases here have
been packaged and validated as a FOMOD far more often than they have been
installed from that zip into a clean profile.** If you are about to publish,
that install is the test that has historically been skipped.

## Build

Two routes. Windows is the normal one:

```powershell
cmake -S plugin -B plugin/build -G "Visual Studio 17 2022" -A x64 `
      -Ddirectxtk_DIR="$PWD/cmake-pkgs/directxtk"
cmake --build plugin/build --config Release
```

Cross-compiling from Linux additionally wants
`-DCMAKE_TOOLCHAIN_FILE=msvc-toolchain.cmake` and an MSVC sysroot at
`/opt/msvc` — the CRT, the MSVC STL headers and the Windows SDK import
libraries. The toolchain file expects `clang-cl`, `lld-link`, `llvm-lib`,
`llvm-rc` and `llvm-mt` on `PATH`.

**A sandboxed session usually cannot build this.** The sysroot is not
checked in and cannot be fetched where the network policy blocks Microsoft's
CDN (`download.visualstudio.microsoft.com`, `aka.ms`) — which is the common
case. GitHub stays reachable, so submodules come down fine and the failure
arrives later, at link, as a wall of `could not open 'kernel32.lib'`. That is
the sysroot missing, not a project fault. Do not go looking for a bug in the
CMake files.

Everything downstream of the DLL is blocked with it: `build_fomod.py` stops at
`no built DLL - build first`, and `validate_fomod.py` then fails because it
validates the *staged* `fomod-build/` tree, not the `fomod/` sources. Source
review, `make_esp.py` and `check_translations.py` all still work.

The first build compiles CommonLibSSE-NG — 363 targets, slow. After that a
source change is seconds. **Do not delete `plugin/build`** to "start clean"
without a reason; reconfiguring in place keeps the object cache. If you only
need the plugin and not a CommonLib source build, `COMMONLIB_PREBUILT_DIR`
(see `CommonLibSSE-NG/cmake/Prebuilt.cmake`) links a published prebuilt
`CommonLibSSE.lib` instead — Release only.

### Submodules

`git clone --recursive`, or `git submodule update --init --recursive`. The
CMake project expects the dependencies as siblings at the repo root, which is
what the submodules give you.

**Do not init them with `--depth 1` and walk away.** A shallow fetch takes the
branch tip, which for `cimgui` and `spdlog` is not the recorded commit; you get
a silently wrong tree that still configures. Check `git submodule status` and
fix any line prefixed `+` by fetching that exact SHA:

```sh
git -C <sub> fetch --depth 1 origin <sha> && git -C <sub> checkout --detach <sha>
```

Five of the seven submodules are real build inputs: `CommonLibSSE-NG`,
`spdlog`, `rapidcsv`, `DirectXTK` (headers, via `cmake-pkgs/directxtk`) and
`cimgui`. **`SKSEMenuFramework` and `SMF-SDK` are reference only** — nothing in
CMake or `plugin/src` includes them. The build links the committed
`extern/lib/SKSEMenuFramework.lib` and includes cimgui's C headers directly.

## Where things live

| File | What it owns |
|---|---|
| `Main.cpp` | plugin declaration, key handling, message hooks |
| `Sense.cpp` | the sweep: candidate scan, shader pool, per-frame tick. **Also all trail state** — marking, gating, polling, the per-actor point lists |
| `Labels.cpp` | everything drawn on the finished frame — tags, ring, vignette, bars, trails |
| `Menu.cpp` | the whole SKSE Menu Framework page. The largest file after `Sense.cpp` |
| `Config.cpp/.h` | every setting, its INI key, its clamp, its comment |
| `Marks.cpp` | user-assignable **name-tag markers** from `ScavengerSense_marks*.ini` |
| `Titles.cpp` | title rules from `ScavengerSense_titles.ini` |
| `Relations.cpp` | the only code that writes to the save |
| `Vision.cpp` | real desaturation, by writing the cinematic parameters the tonemapper reads |
| `PostFX.cpp` | the Community Shaders route, off by default |
| `GameMenus.cpp` | menu awareness, and hiding the game's interface |
| `Locale.cpp` | translated menu text, and `ToUtf8` for game strings |
| `Timing.cpp` | one clock for the whole plugin, freezable |
| `Arousal.cpp` | soft, read-only bridge to SLO Aroused NG |

The headers carry the real explanations — `Vision.h` on the three doors into
the tonemapper and why it drives two at once, `Timing.h` on why one clock,
`Arousal.h` on keeping the dependency soft. Read the header before the `.cpp`.

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
  `=` in the key escaped as `\=`. `tools/check_translations.py` enforces this —
  it exits 1 on anything missing and prints the missing keys pre-escaped, ready
  to paste and translate. Run it before a release.
- **"Mark" means two unrelated things.** `Marks.cpp` is user-assignable pictures
  on name tags, driven by a rule file. `Sense::MarkUnderAim`, `OnTrailMark` and
  `MarkColour` are about tracking quarry and live in `Sense.cpp`. They share no
  code. Grepping for `mark` gets you both.
- **`Labels::Entry` is aggregate-initialised positionally** in `Sense.cpp`. Add
  fields at the END or the initialiser silently shifts.
- **Two threads.** The main thread owns game data; the render thread only reads
  what was handed to it under `Labels::_lock`. Never touch a `TESForm` from
  `Render()`.
- **One clock, and never a private `Now()`.** Use `SS::Now()` from `Timing.h`.
  A per-translation-unit `static const auto epoch` starts ticking on its own
  first call, so two of them disagree by however long you waited, and every
  label looks long expired. `Timing.h` documents the failure in full. Use
  `RealNow()` only for intervals measured against nothing but themselves —
  double-tap detection — and never mix the two.
- **Game strings are not UTF-8.** Non-Latin localisations hand back the system
  code page; feed those bytes to ImGui and you get replacement glyphs. Run them
  through `SS::ToUtf8`.
- **Scaleform menu names are case sensitive**, and the string `Get` overload
  lower-cases. `hideMenus` is read through `Lookup` directly for that reason —
  `validate_fomod.py` knows about this exception, so if you add another, teach
  the validator too.

## Settings precedence

1. Built-in defaults in `Config.h`.
2. `SKSE/Plugins/ScavengerSense_setup/*.ini`, filename order — installer answers.
3. `SKSE/Plugins/ScavengerSense.ini` — the player's own, written whole by the menu.

A preset loaded from the menu deliberately skips step 2.

The author's own tuning is not in the defaults; since 0.8 it ships as a setup
fragment. `fomod/kshakes-preset.ini` is a copy of the live install, written into
the payload as `10-kshakes.ini` by `build_fomod.py` and selected by the
"KShakes's Choice" door on the installer's first page. Refresh that file from
the live install rather than editing it by hand — `build_fomod.py` hard-fails if
it is missing.

## Releasing

After a build, `tools/deploy.ps1` copies the DLL, the esp and the data INIs
into the live MO2 install. It merges rather than overwrites the `[Assigned]`
block of the titles file, so titles typed in game survive a deploy, and it
never touches `ScavengerSense.ini` — that belongs to the game, not the repo.

```sh
python3 tools/check_translations.py   # exits 1 if the menu grew an untranslated string
python3 tools/make_esp.py
python3 tools/build_fomod.py          # needs the built DLL
python3 tools/validate_fomod.py       # run this, it catches real mistakes
```

`build_fomod.py` rebuilds the whole payload tree from the repo every time — DLL
from the last build, esp from `make_esp.py`, data from `mod/` — so a package
cannot ship a stale file. That guarantee is only as good as the build being
current; it copies whatever `plugin/build` last produced without checking that
it is newer than the sources.

`validate_fomod.py` checks schema ordering, that every payload folder is
referenced exactly once, that no two simultaneously-selectable options write the
same file, and that every key in every installer fragment is a setting
`Config.cpp` actually reads. It validates the staged `fomod-build/` tree, so it
only means anything after `build_fomod.py` has run.

And the step the tools cannot do: install the built zip into a clean profile and
answer the installer questions. See the note at the top.

## Habits worth keeping

The bugs found in this project were found by **measuring, not looking**:

- Bar geometry was verified by computing quad areas against length × thickness.
  That caught upright bars rendering at nearly double width — invisible in a
  screenshot, obvious in a number.
- Corner placement was verified by checking all 12 positions land on screen.
- The font was verified by measuring ink coverage, which disproved a "the font
  is too thin" assumption that a visual comparison had seemed to confirm.

When something is geometric or numeric, write the four-line script.

The second habit, visible in the commit history: **when a fix does not hold,
suspect the diagnosis, not the dose.** The 0.7.1 enemy-bar work took five
commits — asking TrueHUD to step aside, then owning the bars, then hiding by
position rather than visibility — before the real answer turned out to be that
the bar was never vanilla and the whole TrueHUD overlay had to go. Each earlier
commit was a plausible refinement of a wrong model. If you are on the third
variation of the same fix, stop and re-derive what is actually drawing.

## Licence

GPL-3.0-or-later, because CommonLibSSE-NG is and this links it statically. The
Modding Exception covers linking against Skyrim, not plugin code. Publishing
this repository is how the Corresponding Source obligation is met — keep the
link on the Nexus page.

MIT/BSD notices for spdlog, DirectXTK and rapidcsv ship in `mod/README.md`.
Dear ImGui is deliberately absent from those: headers only, no object code.
