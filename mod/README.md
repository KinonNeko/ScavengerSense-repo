# Scavenger Sense

**0.5 beta** — by KShakes — https://www.nexusmods.com/profile/KShakes

This is a first public build. Everything in it works and has been played with,
but it has only been tested against one load order. Keep a save from before you
installed it. If something misbehaves, `ScavengerSense.log` (in
`Documents\My Games\Skyrim Special Edition\SKSE\`, and a second copy next to
the plugin) records what happened and is the fastest way to get it fixed.

Press a key and everything worth looting lights up in an outward wave, with a
ring travelling across the ground from your feet, names floating over the
objects, and the colour draining out of the world while it runs.

## Requirements

Required, in the order the game loads them:

- **Skyrim Special Edition or Anniversary Edition**, 64-bit, on Windows. One DLL
  covers every runtime from 1.5.97 to current. Skyrim VR is not built.
- **SKSE64**, matching your runtime.
- **Address Library for SKSE Plugins** — the plugin looks engine functions up by
  ID rather than by hard-coded address, which is what lets one build cover every
  patch. Without it the plugin logs a fatal error and stops.
- **SKSE Menu Framework 3.13 or newer**. The settings page, the name tags, the
  sonar ring and the vignette are all drawn through it. Older builds do not
  export the hook the tags need.
- **`ScavengerSense.esp`, enabled.** The 128 effect shaders that produce the glow
  are records in it, and they cannot be created at runtime. It is ESL-flagged,
  so it costs no load order slot. Without it the menu opens and says "not
  armed", and nothing lights up.

Optional, and detected at runtime:

- **Community Shaders** — nothing is required, but if you use it, turn on
  *Community Shaders mode* on the Look page so the desaturation survives its
  tone mapping.
- **OStim Standalone** — only needed for the Dibella marker in
  `ScavengerSense_marks.ini`. The rule names form lists inside `OStim.esp`; if
  that file is not in your load order the references simply do not resolve and
  the rule sits idle.
- **A font with the glyphs your game's language needs**, for name tags. This mod
  ships one for Simplified Chinese. See the section below.

No script extender plugin other than SKSE, no Papyrus scripts, no SkyUI, no MCM,
no ENB, and no dependency on any particular lighting or weather mod.

## What the installer asks

Three pages. Every answer has a sensible default, so clicking straight through
gives you the mod as intended.

**Page 1 — how it should look.** Four questions, and a reminder of the keys.

| Question | Answers |
|---|---|
| **Name tags** | names, titles and icons · names and titles · names only · none |
| **Screen effect** | full · subtle · darkened edges only · nothing |
| **What lights up** | everything · skip the scenery · containers and valuables only |
| **People** | leave them alone · living people · everyone, bodies included |

None of this is locked in. Every one of these is a control in the settings menu
afterwards, and the page exists so the mod behaves the way you want the first
time you press the key rather than after a trip through the menu.

**Page 2 — menu language.** English or Simplified Chinese.

**Page 3 — optional extras.**

| Option | What it is | Take it if |
|---|---|---|
| **Bundled CJK font** | Noto Sans SC as MainFont.ttf, 2.2 MB | your game is in Chinese, Japanese, Korean, Russian or Thai and item names show as `?` |
| **OStim Standalone** | the Dibella marker and its tally | you have OStim. Without this file the add-on does not exist at all |
| **Oathvein UI palette** | a colour scheme, applied on install | you use Oathvein UI, or just like the palette |

### How the installer's answers reach the game

Each answer drops one small file into
`Data/SKSE/Plugins/ScavengerSense_setup/`, holding only the settings that
answer changes. On startup the plugin reads them in filename order on top of
its built-in defaults, then reads your own `ScavengerSense.ini` over the top of
that. So:

- **Nothing has to be loaded from the menu.** The answers are live on the first
  sweep.
- **Your settings always win.** The moment you press *Save to INI*, that file
  holds every key and the installer's files stop mattering.
- **Deleting one** returns those particular settings to the default.
- A preset loaded from the menu deliberately ignores this folder — a preset is
  meant to describe the whole look on its own.

The Oathvein option uses both routes: it applies the palette immediately
*and* installs the full preset, so you can still switch back and forth from
**Setup → Presets**.

The log says which files it read:

```
config: setup fragment 40-effect.ini (3 keys)
config: no Data/SKSE/Plugins/ScavengerSense.ini yet - running on defaults plus the installer's answers
```

Two more add-ons are built into the plugin and have no files, so the installer
lists them greyed out for information only:

- **SLO Aroused NG** - a flame on the right of a name showing arousal. Appears
  on the Add-ons page as installed or not installed depending on whether you
  have that mod, and hides itself when you do not.
- **Floating subtitles** - hides or lifts a name tag while that person is
  speaking, for anyone running a subtitle mod. Works with any of them; there
  is nothing to detect.

### Bundled font and its licence

The optional font is **Noto Sans SC 2.004**, unmodified, renamed to
`MainFont.ttf` because that is the filename SKSE Menu Framework looks for.
Copyright (c) 2014-2021 Adobe, with Reserved Font Name 'Source'. It is licensed
under the **SIL Open Font License 1.1**, which permits bundling and
redistribution provided the licence text travels with the font - so `OFL.txt`
is installed next to it, along with a note on what the font is and covers.
Please keep those three files together if you repackage anything.

Renaming a file is not a Modified Version under the OFL, so the Reserved Font
Name clause is not engaged. Nothing about the font itself has been altered.

No `ScavengerSense.ini` is installed. The plugin has sensible defaults built in
and writes a fully commented INI the first time you press **Save to INI** in
the menu, so there is never a stale settings file overriding a new default.

Add-ons are always separate files. Any `ScavengerSense_marks_*.ini` next to the
main marker file is read automatically, so deleting one removes that add-on
completely - its rules, its marker and its entry in the menu.

## Keys

- **Y, twice** — run a sweep.
- **A, twice** — the same on a controller.
- **F1** — open the SKSE menu, then **Scavenger Sense → Settings**. F1 belongs
  to SKSE Menu Framework, not to this mod; if it does nothing, check that mod's
  own INI for the key it opens on.

It is a double tap rather than a single press so a key you already use for
something else does not set off a sweep every time you touch it. The two taps
have to land within 0.3 s, and there is a half-second cooldown afterwards.

Both keys, the controller button, the window, and whether it should be a single
press or a hold instead, are all on the Setup page. On a controller, note that A
is also Activate; Right Bumper is unbound in vanilla if you would rather not
share.

## Settings

In game: SKSE menu (F1 by default) → **Scavenger Sense → Settings**. Everything
is live; "Save to INI" writes it to `Data/SKSE/Plugins/ScavengerSense.ini`, which
you can also edit by hand.

## Non-Latin item names — read this if your tags show "???"

This is the one piece of setup that is not automatic, and it is worth
understanding because the symptom is confusing.

Name tags are drawn with whatever font SKSE Menu Framework baked into its ImGui
atlas. If a glyph is not in that atlas, ImGui draws `?`. So on a Chinese,
Japanese, Korean, Russian or Thai copy of the game, tags come out as rows of
question marks — not because the text is wrong, but because the font cannot
draw it.

There is a trap underneath it. SKSE Menu Framework ships with

```ini
PrimaryFont = MainFont.ttf
```

and **ships no `MainFont.ttf`**. When the primary font is missing it silently
falls back to `SkyrimMenuFont.ttf` *with Latin-only glyph ranges* — and it does
that regardless of what `EnableChinese` is set to. So turning `EnableChinese` on
by itself changes nothing. Every stock SMF install is in this state.

**The fix**, which this mod is already 90% of:

1. This mod ships `Data/SKSE/Plugins/fonts/MainFont.ttf` — Noto Sans SC, with
   full ASCII, Latin-1, all 6,763 GB2312 hanzi and CJK punctuation. Because Mod
   Organizer merges directories, it lands in the same virtual folder SMF reads,
   without modifying SMF itself.
2. In `Data/SKSE/Plugins/SKSEMenuFramework.ini`, set:
   ```ini
   [Fonts]
   PrimaryFont  = MainFont.ttf
   EnableChinese = true
   ```
   (Use `EnableJapanese` / `EnableKorean` / `EnableCyrillic` / `EnableThai`
   instead, or as well, for other languages — but supply a font that actually
   contains those glyphs. The bundled one is Simplified Chinese.)
3. Consider lowering `FontSizeMedium`. SMF rasterises the primary font once per
   Font Awesome file it finds, so with CJK ranges on you get roughly four full
   bakes of a several-thousand-glyph font. At the default 32px that is a font
   texture around 144 MB; at 24px it is about 80 MB. 24 is a good compromise.

The plugin checks this for you. On the first HUD frame it looks for U+4E00 in
the loaded font and writes one of these to the log:

```
labels: font has CJK glyphs, non-Latin item names will render
labels: the menu framework's font has no CJK glyphs - names in Chinese, ...
```

and the same verdict appears in the Name tags section of the settings page.

Menu *translations* are separate and independent of all this: set `language` in
the settings page or the INI, and the plugin reads
`Data/SKSE/Plugins/ScavengerSense_<language>.ini`. English and Chinese ship with
the mod.

## Community Shaders and the desaturation

The desaturation writes the game's cinematic image space parameters — the same
input the tonemapper reads. On a stock game that works. Under Community Shaders
it does not, and there is no way to make it: CS exposes no plugin API at all,
its feature list is compiled in, and shipping an HLSL for it to pick up is
rejected by a name whitelist.

So there is a second route, off by default:

```ini
[Tint]
postProcess = true
```

This runs our own full-screen shader pass at the very end of the frame, through
an ImGui draw callback, after everything else has had its turn. It copies the
finished frame, samples the copy, and writes back a genuinely desaturated,
graded and vignetted result. Nothing downstream can undo it because there is
nothing downstream. It also takes over the vignette and does it properly, as a
radial falloff rather than four overlapping gradient quads.

It is off by default because it is the only part of this mod that talks to the
graphics pipeline directly. Some notes on how it behaves:

- Any unrecoverable problem — shaders that will not compile, a device that
  refuses a resource — disables it for the rest of the session, logs the reason,
  and falls back to the overlay vignette. It never half-works.
- A frame it cannot use (nothing bound, a multisampled target) is skipped and
  retried, not treated as fatal.
- The shaders are compiled at load, not on first use, so there is no hitch the
  first time you sweep.
- The settings page shows its live state: `ready`, `running at 2560x1440`,
  `waiting: ...`, or `unavailable: ...`.

If you are not running Community Shaders or ENB, leave it off — the image space
route is lighter and already works.

## Diagnostics

Two logs, identical contents:

- `Documents/My Games/Skyrim Special Edition/SKSE/ScavengerSense.log`
- `Data/SKSE/Plugins/ScavengerSense-diag.log` — under Mod Organizer this lands in
  the `overwrite` folder, which is much easier to get at.

Set `debug = true` in `[General]` for per-object detail.

Useful lines even without debug: the plugin index the esp resolved to, how many
effect shaders it found, what the scan visited and why it rejected things, when
menus open and whether we judged them blocking, and a readback of what the
engine actually resolved the image space to while desaturating.

## If the sweep ever stops working

`[General] menuAware = false` turns off menu detection entirely. It exists
because a mod that parks a permanent overlay menu on the menu stack could,
in principle, be misjudged as "a menu is open" forever. There is also a
watchdog that cancels any wave still running 60 seconds past its budget and
logs what was holding it.
