# Scavenger Sense

**0.10.1 beta** — by KShakes.

Press a key to send a sensing wave through nearby loot and people, with optional
name tags, tracking and HUD bars. This local build has passed offline checks;
the new features still need gameplay verification. Source and release details:
https://github.com/KinonNeko/ScavengerSense-repo

## Requirements

- Skyrim SE/AE on Windows, with SKSE64 and Address Library matching your exact
  executable. Offline runtime probes cover 1.5.97, 1.6.1170 and 1.7.104; they
  do not establish successful startup on every user load order. VR is not built.
- SKSE Menu Framework with HUD exports and the matching ImGui ABI. The updated
  local 3.14 DLL was checked against this build. Version numbers alone are not
  enough; the plugin checks ImGui version/layout before registering callbacks.
- The included ScavengerSense.esp enabled. Its effect-shader records are required.
- A font with the glyphs your item/NPC names need, plus matching framework glyph
  ranges. The optional bundled Noto font supports Simplified Chinese, not every
  language. See Fonts below.

TrueHUD, Community Shaders, OStim Standalone and SLO Aroused NG are optional
integrations. SkyUI/MCM is not required. See docs/DEPENDENCIES.txt for startup
errors and logs. Mod contents install at Skyrim's Data root; no extra nested
Data folder is required when installing through MO2.

## Installer flow

The first page chooses English or Chinese for the following installer pages and
sets the initial menu language for a fresh installation. An existing personal
INI still wins.

1. **Language.** English or Simplified Chinese.
2. **Starting preset and keys.** Choose the author's preset, Only sensing,
   Sensing and tracking, or Choose everything myself.
3. **Controls and display guide.** Keep shared-key double tap, choose immediate
   single press, or hold for 0.35 seconds. Single/hold choices give tracking its
   own key (middle mouse; controller tracking starts unbound). This page also
   explains lighting, actor-only smoothing, dual equipment and upgrades for
   every preset, including quick installs.
4. **Optional extras.** Font and optional OStim files, plus built-in integration
   information. A quick-preset route has four visible pages in total.

Choose everything myself inserts three detailed pages before Optional extras:
tracking; appearance/HUD; and bars/sound. That route has seven visible pages.

| Preset | Starting behaviour |
|---|---|
| KShakes's Choice | Author's tuned sensing/tracking, combat and overhead bars, stats, equipment icons, and 0.26 s actor smoothing. Combat bars take over enemy HUD display while enabled. |
| Only sensing | Sensing with living people; no trails, combat-bar takeover or vanilla HUD hiding. Smoothing is 0. |
| Sensing and tracking | The same sensing defaults plus crouch-and-sense tracking. Smoothing is 0. |

All three explicitly enable the screen master at strength 1, object glow and
the actors-only smoothing scope. A smoothing duration of 0 still means no
smoothing; the scope does not turn it on by itself. Each option remains
adjustable in the menu. The two sensing installer fragments use the same source
files as their loadable presets; the author's installer/menu values are checked
for equality.

Detailed installation offers full/subtle/vignette-only/no screen grading.
Subtle sets the screen master strength to 0.5; No screen effects turns the
master off. Object glow remains independent. Leave people alone explicitly
turns actor highlighting off. Sweep bars require highlighting people on that
page. Silence is the default; selecting the two-note chime explicitly enables
sound. The combat option labelled from your first hit uses the struck trigger.

## Settings, upgrades and presets

The framework opens its menu with its configured binding (normally F1). Choose
Scavenger Sense -> Settings. Useful locations:

- **Keys:** all keyboard/controller bindings and single/double/hold gestures.
- **Look -> Lighting controls:** screen master, 0-1 strength and Keep original
  brightness. Brightness 1 is neutral; lower darkens, higher brightens. The
  shortcut resets brightness/contrast, removes colour cast and disables the
  vignette while retaining desaturation.
- **Look -> Vision:** object glow switch and strength. Zero turns highlights
  off; positive strength changes apply to newly highlighted objects.
- **Names -> Name tags:** smoothing duration and Smooth characters and creatures
  only (default on). People/animals retain smoothing; static items follow the
  camera directly. Disable the scope to smooth all targets.
- **Names -> About everyone:** equipment icons. Left/right weapons, spells,
  staves, shield and torch are separate; two-handed weapons use one icon. Each
  hand keeps its own enchantment/spell colour and charge. Custom markers keep
  their icon priority. With racial emblems enabled, the corner stats row leaves
  equipment to the overhead chip, as before.
- **Bars -> Bar style:** independent player/NPC bar sizes, offsets, number
  centring and PNG layers. NPC values can show current, current/max or percent;
  per-second regeneration figures are not included. See docs/CUSTOMISING.txt.
- **Setup:** individual HUD suppression and named presets. Keep compass/messages
  by disabling their hiding checkboxes while leaving other parts suppressed.

At game startup, configuration is read in this order:

1. Built-in defaults.
2. Installer fragments in SKSE/Plugins/ScavengerSense_setup, sorted by filename.
3. Your personal SKSE/Plugins/ScavengerSense.ini, overriding matching keys.

When changing installer answers, replace the previous package files instead of
merging old ScavengerSense_setup fragments. Otherwise unselected old answers
can remain active. Back up custom rules/artwork stored inside that package
before replacing it. Keep personal settings in your separate override location.

Reinstalling does not overwrite the personal INI. If you have saved one already,
new installer choices may not visibly apply: use the menu to change those saved
settings. This is intentional. Removing one installer fragment removes that
answer; a personal value can still override the resulting default.

Loading a named preset from Setup -> Presets is different: it starts from
built-in defaults, then applies the selected file, ignoring installer fragments
and previous unsaved settings. Missing keys reset to defaults; a small preset
is not a partial merge. Check language and keys after loading. **Save to INI**
keeps your result across restarts; it does not save automatically on preset load.

## Default gestures

With shared keys: double-tap Y (controller A) senses; a single press marks or
releases a readable person/trail after the double-tap window; holding clears
marks and trails. Crouch and sense to read ordinary footprints. Marked targets
remain trackable according to the tracking settings. If you choose single-press
sensing, it starts without the double-tap delay and tracking uses a separate key.
Controller A also activates things in vanilla; choose another binding if needed.
The framework's menu binding is configured by that framework.

## Fonts and translations

The optional MainFont.ttf is Noto Sans SC 2.004, with its OFL.txt and font note.
It covers Latin and the bundled Simplified Chinese character set; other scripts
need a suitable font. Installing a font does not modify framework configuration.
In the effective SKSE/Plugins/SKSEMenuFramework.ini set:

```ini
[Fonts]
PrimaryFont = MainFont.ttf
EnableChinese = true
```

Framework defaults can change between releases; inspect the effective file
rather than assuming its default font. Keep other framework settings unless
you intend to change them. Restart Skyrim/SKSE after changing font files/ranges.
The bundled font is not a promise of coverage for all modded names.

Menu translations are independent of fonts and game-language names. Copy the
English template in SKSE/Plugins/ScavengerSense/translations next to the DLL as
ScavengerSense_<language>.ini, translate values in UTF-8 and use the language
rescan control. Missing entries fall back to English. See docs/TRANSLATING.txt
and docs/中文说明.txt. PNG bar/arousal skins are documented in CUSTOMISING.txt;
replacing already cached artwork requires a restart.

## Graphics and troubleshooting

If native image-space grading does not visibly work with your graphics setup,
Look -> Screen tint offers Community Shaders mode (own shader pass). It is
optional. The screen master covers grading and vignette through either route;
legacy image-space mode reads positive slider changes on the next sweep, while
turning the master off stops it. Actual appearance depends on the load order.

Logs are written to Documents/My Games/Skyrim Special Edition/SKSE/
ScavengerSense.log and SKSE/Plugins/ScavengerSense-diag.log (normally MO2
Overwrite). An import failure can occur before the plugin creates a log. For
DLL errors, include the exact message, skse64.log, the plugin log if created,
and your runtime/framework versions. No new save, BodySlide or behaviour
conversion is required for these updates.

## Licence

Scavenger Sense is **GPL-3.0-or-later**, because it links CommonLibSSE-NG.
The full licence text is at `SKSE/Plugins/ScavengerSense/LICENSE.txt` and the
source is at the link on the [mod page](https://www.nexusmods.com/skyrimspecialedition/mods/188380).

`ScavengerSense.dll` is a combined work. Three components are compiled into it
under permissive licences that ask for their notice to travel along; that is
what the rest of this section is. Drawing executes in SKSE Menu Framework;
matching Dear ImGui/cimgui headers are used by the plugin. The ImGui MIT notice
is included in SKSE/Plugins/ScavengerSense/docs/LICENSE-imgui.txt. The bundled
font's OFL sits beside it in SKSE/Plugins/fonts/.

<details>
<summary>spdlog — MIT</summary>

    The MIT License (MIT)

    Copyright (c) 2016 Gabi Melman.                                       

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.

    -- NOTE: Third party dependency used by this software --
    This software depends on the fmt lib (MIT License),
    and users must comply to its license: https://raw.githubusercontent.com/fmtlib/fmt/master/LICENSE

</details>

<details>
<summary>DirectXTK — MIT</summary>

    MIT License

        Copyright (c) Microsoft Corporation.

        Permission is hereby granted, free of charge, to any person obtaining a copy
        of this software and associated documentation files (the "Software"), to deal
        in the Software without restriction, including without limitation the rights
        to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
        copies of the Software, and to permit persons to whom the Software is
        furnished to do so, subject to the following conditions:

        The above copyright notice and this permission notice shall be included in all
        copies or substantial portions of the Software.

        THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
        IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
        FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
        AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
        LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
        OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
        SOFTWARE

</details>

<details>
<summary>rapidcsv — BSD-3-Clause</summary>

    BSD 3-Clause License

    Copyright (c) 2017, Kristofer Berggren
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this
      list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

    * Neither the name of the copyright holder nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
    OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

</details>


## Track appearance

Tracks -> Drawing -> Drawn as: choose **Footprints** for alternating prints
instead of chevrons. KShakes Choice already selects footprints. Print size and
Print spacing are adjustable. Turn **Connect lit tracks** off for separate
prints without the connecting glow. The custom installer offers both styles.

For custom boot soles or paws, place a transparent square PNG below
`SKSE/Plugins/ScavengerSense/icons` and enter its relative path in **Footprint PNG**.
Draw a right print with toes pointing UP; left prints mirror automatically.
White artwork takes the trail colour. Blank/missing files use built-in prints.
Restart Skyrim after replacing artwork already loaded in this session.
These are projected tracking marks, not permanent ground decals; recording
still starts at first contact and only follows loaded targets. No custom boot
artwork or per-species texture selection is bundled. Save to INI after changes.
