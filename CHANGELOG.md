# Changelog

Every published version, newest first. Versions are the ones that reached
Nexus; the numbering skips where a build was cut but never shipped.

The version lives in three places that must agree, and a release is not a
release until they do: `REL::Version` in `plugin/src/Main.cpp`,
`MachineVersion` in `fomod/info.xml`, and `project(... VERSION)` in
`plugin/CMakeLists.txt`.

- **Tags and bars can hold still.** Two switches beside "Tags follow what
  they name": hang them a standing head's height over the feet instead of
  over the head, which bobs with every step - a crouch or a fall still
  brings them down once the head has sunk forty units; and ease their movement on
  screen over a fraction of a second, so what bob is left becomes a sway.
  Both off by default; both apply to tags, combat bars and your own.
- **Six settings that reset on every load since 0.9.** The perk section
  was written into the middle of the player section, so your level,
  weapon and race icon choices, the stats row's place and the cold cap
  landed under `[Perks]` and were read from `[Player]` - and came back
  as defaults every time. Written where they are read now, and a file
  from those versions is read from where it put them.
- **The author's preset is the author's current game**, every setting,
  including the new ones: the lock-on mark, the glyph colours, the
  cooldown burst, the steady anchor.
- **An ash pile whose body is gone is an empty one.** After a reload the
  pile's link to the body it came from can point at nothing; the pile
  used to fall through to "never had an inventory" and come back as a
  name. A link with nobody on the other end now reads as empty, and the
  first twenty piles a session judges are logged with the verdict.
## 0.9.5 beta

Three from the feedback, all of them about somebody else's interface or
the bars over somebody else's head.

- **Bars over people who were not fighting you.** "Anyone fighting me" read
  the engine's combat flag with the player as target, and that flag is true
  of more than fighting: a deer running from you carries it, and so does a
  wolf that lost you a while ago and is searching. Worse, the seed refreshed
  the entry every tick, so the two-minute staleness cap - which existed to
  let go of a combat flag that never clears - never got a say, and a stuck
  flag was a bar for the rest of the session. That is the "every hostile
  creature, permanently" some people saw, vampires included. The seed now
  asks the engine's own combat state: not fleeing, target not lost, and
  the combat group's entry for you marked known - a cave that heard
  something and went looking is in combat with you on every other count,
  and nobody in it has seen you. The same question holds a bar up for anyone who arrived that way;
  somebody you have actually hit keeps theirs on the plain "in combat with
  you", fleeing included, because a wounded deer running is the hunt and
  not the end of it. "Only enemies" on the sense asks the stricter question
  too, so a fleeing deer no longer counts as an enemy there either. With `debug` on, the log names everyone who gets a
  bar without a hit and says why.
- **The game's HUD and TrueHUD's flashed on leaving a container.** Both are
  handed back visible by their owners when a menu closes, and this mod took
  them away again on its next pass - a frame later for the vanilla HUD, and
  up to half a second later for TrueHUD's movie and the vanilla enemy
  health element, which were only re-asserted on a pulse. Three changes:
  the enemy-bar hold runs every tick and not on the pulse; the menu
  open/close sink re-asserts everything on the spot, before the frame
  draws; and the vanilla HUD is hidden at its main clip as well as its
  movie, because the clip is not what the game hands back.
- **The Dibella marker is a lily now, and every marker's picture is yours
  to pick.** The file called dibella.png had been a heart all along. The
  heart is still shipped, as heart.png, and each marker has a "Symbol" box
  on the Custom markers page: the marker file's own picture, the built-in
  heart, or any PNG in the icons folder - your own included. Saved with
  the marker's other styling, so it travels with a preset.
- **An aura was a hit, and every hostile in reach wore a bar.** Measured
  rather than reasoned this time: with "only once you have hit them" on,
  the log showed a spell ability pulsing a hit event, player as cause, on
  every draugr within four thousand units every half second, none of them
  in combat, refreshing each entry faster than the linger could drop it.
  That is the "every hostile creature, permanently" report, and the 0.9.1
  seed change above never touched it because the seed is not where these
  came from. A hit on somebody who is not fighting anybody now counts only
  from a weapon or bare hands; a spell earns its bar once its target is in
  a fight. The first forty hits of a session are logged with their source,
  distance and the target's combat state, so the next one of these is a
  read and not a theory.
- **Somebody you had hit kept a bar after losing you.** The stricter question
  above only guarded the people who arrived without a hit; a wolf you had
  struck once was held by the bare combat flag, searching or stuck, for the
  two minutes of the staleness cap. It now has to still know where you are
  - fleeing still counts, searching does not. Every change of verdict is
  logged with the engine's answers as they were.
- **TrueHUD had the last word on a menu change.** Sinks run in the order they
  were registered, SKSE loads plugins alphabetically, and S comes before T:
  on every menu open or close TrueHUD showed its movie again one step after
  this mod hid it, and the next tick took it away - a frame or two of bar.
  The sink now moves itself behind everyone else once the game is running,
  and with `debug` on the log counts how often the movie was found visible
  again, and whether the sink or the tick caught it.
- **The activation prompt can be moved.** The "Talk  Lydia" line is the
  game's, not this mod's, and it sits on the crosshair - which is to say
  on the face of whoever you are about to talk to. Six offsets under
  `[General]`, and the same six on the Interface page: the whole prompt
  across and down, then on top of that the name line across and down, the
  key glyph down, and the value/weight line down. The glyph gets no across
  of its own because the HUD's script pins it beside the name on every
  update. Nothing in that script writes the other positions, so the
  offsets hold; they are re-checked every tick because a HUD reload
  rebuilds the elements where the file put them. All zero, the default,
  touches nothing.

## 0.9 beta

Four things people asked for, one of them a bug that had the feature exactly
backwards.

- **"Living only" and "dead only" were reversed, wholesale.** `Actor::IsDead()`
  reports every living NPC as a corpse - a defect found and written down here
  once before, when the tag colour was moved off it and onto the life state.
  The filter was left behind on the old call, so "living only" hid the living
  and "dead only" showed them. Both now ask one shared question.
- **A perk can gate the sense and the hunt.** Name one in `[Perks]` - by the
  name the game shows, or as `Plugin.esp|0x1234` - and the key does nothing
  until your character has it, with an optional line on screen saying why.
  The perk comes from whatever mod you already run: nothing is added to the
  save, no script, no record, and no conflict with a perk overhaul. Empty is
  no requirement, and a name that matches nothing leaves the gate open rather
  than shut - a typo must not lock you out of your own mod.
- **Numbers on your own bars**, the same four choices the bars over other
  people have had. Yours and theirs are separate settings under their own
  headings, and yours reaches all three places your bars appear: under your
  name, over your head, and pinned to a corner.
- **An add-on whose mod is missing no longer vanishes.** A rule that names a
  plugin nobody has resolves none of its forms, so it matched nothing and was
  dropped before the availability check ever saw it - which is why the OStim
  add-on could be installed and absent from the Add-ons page at the same time.

- **The mod now says when the menu framework cannot draw your language.**
  A font can hold every character a script needs and still draw none of
  them: SKSE Menu Framework builds its atlas from switches in its own INI,
  and the non-Latin ones are off out of the box. The result is mojibake
  everywhere, which looks like a text-encoding fault - the hunt for it went
  through code pages, fonts and an input method before reaching one line in
  somebody else's config. It is read at startup now, checked against both
  the menu language and the game's own, and named on screen and in the log.
  Nothing is written to that file: it also holds the player's menu key and
  theme, and none of that is ours.
- The loading banner carried its own copy of the version number and had
  gone a whole release stale, saying 0.8 while the plugin declared 0.9. It
  was a fourth version site nobody had counted; it reads the declared one
  now.

The menu was rearranged around what went wrong above. Ten pages became nine,
each named in one word, and none can scroll off the edge behind an arrow.
Options no longer disappear when the switch above them is off - they grey out,
because an option you have never switched on was otherwise an option you had
never seen. The perk boxes sit at the top of the Keys page in a section of
their own: a perk requirement is not a control, it is the reason a control
does nothing. And the first page is named for what the mod calls the feature
everywhere else - the sense, not the sweep, which was only ever the
implementation's word for one firing of it.

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
