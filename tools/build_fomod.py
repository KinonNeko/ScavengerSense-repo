#!/usr/bin/env python3
"""Rebuild the FOMOD payload tree and ModuleConfig.xml.

Every answer on the "How it should look" page installs one small fragment into
SKSE/Plugins/ScavengerSense_setup/. The plugin merges those over its built-in
defaults before it reads the player's own INI, so the mod behaves the way they
asked the moment the game starts - nothing has to be loaded from the menu.
Fragments carry only the keys their answer changes.
"""
import os, re, shutil, sys

ROOT  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOD   = os.path.join(ROOT, "mod")
BUILD = os.path.join(ROOT, "fomod-build")
SETUP = "SKSE/Plugins/ScavengerSense_setup"

# ---------------------------------------------------------------- fragments
HEAD = ("; ============================================================================\n"
        ";  Scavenger Sense - installer answer: {title}\n"
        ";\n"
        ";  Written by the FOMOD, not by you. It holds only the settings this\n"
        ";  answer changes; everything else stays at the built-in default.\n"
        ";  Your own ScavengerSense.ini overrides this file key by key, so once\n"
        ";  you press \"Save to INI\" in the menu this file stops mattering.\n"
        ";  Delete it to go back to the defaults for these settings.\n"
        "; ============================================================================\n\n")


def fragment(title, body):
    return HEAD.format(title=title) + body.strip() + "\n"


# folder -> (fragment filename, title, body).  A folder of None means the answer
# is the built-in default and installs nothing at all.
FRAGMENTS = {
    "10 Tags - no icons": ("30-tags.ini", "name tags without icons", """
[Labels]
icons = false
"""),
    "11 Tags - names only": ("30-tags.ini", "names only, no titles or icons", """
[Labels]
icons = false

[Titles]
show = false
"""),
    "12 Tags - none": ("30-tags.ini", "no name tags", """
[Labels]
enable = false

[Titles]
show = false
"""),

    "20 Effect - subtle": ("40-effect.ini", "a gentler screen effect", """
[Tint]
saturation = 0.65
strength = 0.70
overlayStrength = 0.18
"""),
    "21 Effect - vignette only": ("40-effect.ini", "vignette only, colour left alone", """
[Tint]
enable = false
overlay = true
"""),
    "22 Effect - none": ("40-effect.ini", "no screen effect", """
[Tint]
enable = false
overlay = false
"""),

    "30 Lights - no scenery": ("50-lights.ini", "skip doors, plants and activators", """
[Categories]
door = false
flora = false
activator = false
furniture = false
"""),
    "31 Lights - containers and valuables": ("50-lights.ini", "containers and valuables only", """
[Categories]
container = true
valuable = true
door = false
flora = false
gear = false
alchemy = false
book = false
activator = false
furniture = false
"""),

    "40 People - living": ("60-people.ini", "light up living people", """
[Categories]
actor = true
actorFilter = living
"""),
    "41 People - everyone": ("60-people.ini", "light up everyone, dead included", """
[Categories]
actor = true
actorFilter = all
"""),

    "50 Sweep bars - hostile": ("70-sweepbars.ini", "sweep bars over hostiles", """
[Vitals]
actors = true
hostileOnly = true
"""),
    "51 Sweep bars - everyone": ("70-sweepbars.ini", "sweep bars over everyone lit", """
[Vitals]
actors = true
hostileOnly = false
"""),

    "52 Combat bars": ("71-combatbars.ini", "bars over people you have hit", """
[Vitals]
combat = true
"""),

    "53 My overhead bars": ("72-overhead.ini", "bars over your own head", """
[Self]
overhead = true
"""),

    "54 Survival - plain": ("73-survival.ini", "plain dead zone on the bars", """
[Vitals]
lostFx = false
"""),
    "55 Survival - off": ("73-survival.ini", "no dead zone on the bars", """
[Vitals]
lostMax = false
"""),

    "56 Chime - off": ("74-chime.ini", "no sweep chime", """
[Sound]
enabled = false
"""),

    # The quick-start presets are only bundles of the same answers the
    # detailed pages give one at a time.
    "05 Preset - sense": ("20-preset.ini", "preset: the sense", """
[Categories]
actor = true
actorFilter = living
"""),
    "06 Preset - sense HUD": ("20-preset.ini", "preset: the sense, plus a sense-built HUD", """
[Categories]
actor = true
actorFilter = living

[Vitals]
actors = true
actorsAll = true
hostileOnly = true
actorsWhen = always
combat = true
combatAll = true

[Self]
overhead = true
"""),
    "07 Preset - hide HUD": ("21-hidehud.ini", "hide the game's own interface", """
[General]
hideGameHud = true
"""),
}


def sync_payload():
    """Rebuild every non-fragment payload folder from the repo, so the tree
    can never ship a stale DLL, esp or data file. The DLL comes from the last
    build, the esp from make_esp.py's output at the repo root."""
    plugins = os.path.join(MOD, "SKSE/Plugins")

    for d in ("00 Core", "01 Chinese menu", "02 CJK font", "03 OStim add-on",
              "04 Oathvein preset", "fomod"):  # 04 lingers from older trees
        p = os.path.join(BUILD, d)
        if os.path.isdir(p):
            shutil.rmtree(p)

    def put(src, dst):
        if not os.path.exists(src):
            sys.exit(f"missing payload source: {src}")
        dst = os.path.join(BUILD, dst)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)

    dll = next((p for p in (os.path.join(ROOT, "plugin/build/Release/ScavengerSense.dll"),
                            os.path.join(ROOT, "plugin/build/ScavengerSense.dll"))
                if os.path.exists(p)), None)
    if not dll:
        sys.exit("no built DLL - build first")
    put(dll, "00 Core/SKSE/Plugins/ScavengerSense.dll")
    put(os.path.join(ROOT, "ScavengerSense.esp"), "00 Core/ScavengerSense.esp")
    put(os.path.join(MOD, "README.md"), "00 Core/README.md")
    put(os.path.join(ROOT, "LICENSE"), "00 Core/SKSE/Plugins/ScavengerSense/LICENSE.txt")
    put(os.path.join(plugins, "ScavengerSense_marks.ini"),
        "00 Core/SKSE/Plugins/ScavengerSense_marks.ini")
    put(os.path.join(plugins, "ScavengerSense_titles.ini"),
        "00 Core/SKSE/Plugins/ScavengerSense_titles.ini")
    icons = os.path.join(plugins, "ScavengerSense/icons")
    for icon in sorted(os.listdir(icons)):
        put(os.path.join(icons, icon), f"00 Core/SKSE/Plugins/ScavengerSense/icons/{icon}")
    put(os.path.join(MOD, "Sound/FX/ScavengerSense/sweep.wav"),
        "00 Core/Sound/FX/ScavengerSense/sweep.wav")
    put(os.path.join(MOD, "Sound/FX/ScavengerSense/README-sound.txt"),
        "00 Core/Sound/FX/ScavengerSense/README-sound.txt")

    put(os.path.join(plugins, "ScavengerSense_chinese.ini"),
        "01 Chinese menu/SKSE/Plugins/ScavengerSense_chinese.ini")
    for f in ("MainFont.ttf", "OFL.txt", "README-font.txt"):
        put(os.path.join(plugins, "fonts", f), f"02 CJK font/SKSE/Plugins/fonts/{f}")
    put(os.path.join(plugins, "ScavengerSense_marks_ostim.ini"),
        "03 OStim add-on/SKSE/Plugins/ScavengerSense_marks_ostim.ini")
    # The Default preset ships with everybody: it is how the menu gets back
    # to the shipped look after experiments.
    put(os.path.join(plugins, "ScavengerSense/presets/Default.ini"),
        "00 Core/SKSE/Plugins/ScavengerSense/presets/Default.ini")
    put(os.path.join(ROOT, "fomod/info.xml"), "fomod/info.xml")
    for img in sorted(os.listdir(os.path.join(ROOT, "fomod/images"))):
        put(os.path.join(ROOT, "fomod/images", img), f"fomod/images/{img}")


def write_payload():
    for folder, (name, title, body) in FRAGMENTS.items():
        path = os.path.join(BUILD, folder, SETUP)
        os.makedirs(path, exist_ok=True)
        open(os.path.join(path, name), "w", encoding="utf-8").write(fragment(title, body))


# ------------------------------------------------------------------ the XML
def esc(t):
    return t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def plugin(name, desc, folder=None, type_="Optional", flags=None, dep=None, image=None):
    x = f'            <plugin name="{esc(name)}">\n'
    x += f"              <description>{esc(desc.strip())}</description>\n"
    if image:
        x += f'              <image path="{esc(image)}" />\n'
    if folder:
        x += ("              <files>\n"
              f'                <folder source="{esc(folder)}" destination="" priority="0" />\n'
              "              </files>\n")
    if flags:
        x += "              <conditionFlags>\n"
        for k, v in flags.items():
            x += f'                <flag name="{k}">{v}</flag>\n'
        x += "              </conditionFlags>\n"
    x += "              <typeDescriptor>\n"
    if dep:
        x += ("                <dependencyType>\n"
              f'                  <defaultType name="{type_}" />\n'
              "                  <patterns>\n                    <pattern>\n"
              '                      <dependencies operator="And">\n'
              f'                        <flagDependency flag="{dep[0]}" value="{dep[1]}" />\n'
              "                      </dependencies>\n"
              f'                      <type name="{dep[2]}" />\n'
              "                    </pattern>\n                  </patterns>\n"
              "                </dependencyType>\n")
    else:
        x += f'                <type name="{type_}" />\n'
    x += "              </typeDescriptor>\n            </plugin>\n"
    return x


def group(name, kind, plugins):
    return (f'        <group name="{esc(name)}" type="{kind}">\n'
            '          <plugins order="Explicit">\n\n'
            + "\n".join(plugins) +
            "\n          </plugins>\n        </group>\n")


def step(name, groups, when=None):
    x = f'    <installStep name="{esc(name)}">\n'
    if when:
        x += ("      <visible>\n"
              f'        <flagDependency flag="{when[0]}" value="{when[1]}" />\n'
              "      </visible>\n")
    x += ('      <optionalFileGroups order="Explicit">\n\n'
          + "\n".join(groups) +
          "\n      </optionalFileGroups>\n    </installStep>\n")
    return x


KEYS = """Nothing to tick - this is just so you know what to press.

    Y, twice     run a sweep. Keyboard.
    A, twice     the same, on a controller.
    F1           open the settings menu, then pick Scavenger Sense - Settings.

It is a double tap rather than a single press so that a key you already use for
something else does not set off a sweep every time you touch it. The two taps
have to land within a third of a second, and there is a half-second cooldown
afterwards.

Both keys, the controller button, the double tap window, and whether it should
be a single press or a hold instead, are all on the Setup page in the menu. On a
controller, note that A is also Activate - if that bothers you, Right Bumper is
free in vanilla and one click away in the menu.

If F1 does nothing, that key belongs to SKSE Menu Framework rather than to this
mod - check its own INI for the key it opens on."""


def build_xml():
    tags = group("Name tags over what lights up", "SelectExactlyOne", [
        plugin("Names, titles and icons",
               "The full tag: the object or person's name, any title they have earned "
               "underneath it, and the small icons - a Dibella sigil, a wedding ring, an "
               "arousal flame - to the side.\n\nThis is how the mod is meant to look.",
               type_="Recommended"),
        plugin("Names and titles, no icons",
               "Names and titles, but no icons beside them.\n\nPick this if you find the "
               "sigils and flames busy, or if you are not running any of the mods they "
               "come from.",
               "10 Tags - no icons"),
        plugin("Names only",
               "Just the name of the thing. No titles above people, no icons.\n\n"
               "The cleanest option that still tells you what you are looking at.",
               "11 Tags - names only"),
        plugin("No name tags at all",
               "Things light up, but nothing is written over them. You get the glow, the "
               "ring travelling across the ground, and the colour draining out of the "
               "world, and you work out what is what yourself.\n\nAlso the cheapest - "
               "drawing tags is the only part of a sweep that costs anything per frame.",
               "12 Tags - none"),
    ])

    effect = group("Screen effect while a sweep runs", "SelectExactlyOne", [
        plugin("Full - colour drains and the edges darken",
               "The world goes almost grey and the edges of the screen darken for as long "
               "as the sweep lasts, so the glow is the only colour left.\n\nThis is the "
               "effect the mod is built around.",
               type_="Recommended"),
        plugin("Subtle",
               "The same effect at roughly half strength. The world dims rather than "
               "draining.\n\nA good middle if the full version is too much on a bright "
               "monitor.",
               "20 Effect - subtle"),
        plugin("Darkened edges only",
               "The colour is left alone; only the edges of the screen darken.\n\nWorth "
               "picking if you run ENB or a weather mod whose grading you do not want "
               "touched.",
               "21 Effect - vignette only"),
        plugin("Nothing - leave my screen alone",
               "No colour change and no darkening. Objects glow, the ring runs across the "
               "ground, and nothing else about the image changes.\n\nThe safe answer for "
               "heavily graded setups.",
               "22 Effect - none"),
    ])

    lights = group("What lights up", "SelectExactlyOne", [
        plugin("Everything worth picking up",
               "Containers, doors, plants and ore, weapons and armour, ingredients and "
               "potions, books, gold and gems, and things you can use like levers and "
               "bookshelves.\n\nAll of it is a checkbox in the menu afterwards.",
               type_="Recommended"),
        plugin("Skip the scenery",
               "As above, but doors, plants and usable objects stay dark.\n\nPick this if "
               "a sweep in a town lights up forty doors and you only wanted the loot.",
               "30 Lights - no scenery"),
        plugin("Containers and valuables only",
               "Chests, barrels, sacks, urns, and gold, gems and jewellery. Nothing "
               "else.\n\nThe answer for someone who only wants to know whether a room is "
               "worth searching.",
               "31 Lights - containers and valuables"),
    ])

    people = group("People", "SelectExactlyOne", [
        plugin("Leave people alone",
               "A sweep finds objects only. Nobody is outlined and no names float over "
               "anyone.\n\nThe lightest option, and the one that changes the least about "
               "how the game reads.",
               type_="Recommended"),
        plugin("Light up living people",
               "People are outlined too, coloured by how they feel about you - lovers, "
               "friends, rivals, enemies - with their name, their title, and any marker "
               "they have earned above them.\n\nThis is what the titles, the relationship "
               "colours and the add-ons are for. Corpses stay dark.",
               "40 People - living"),
        plugin("Light up everyone, bodies included",
               "As above, and dead bodies light up in their own colour as well.\n\nUseful "
               "after a fight in long grass. Slightly busier everywhere else.",
               "41 People - everyone"),
    ])

    keys = group("Default keys", "SelectAny", [
        plugin("Y sweeps, F1 opens the settings", KEYS, type_="NotUsable"),
    ])

    language = group("Language of the settings menu / 设置菜单的语言", "SelectExactlyOne", [
        plugin("English",
               "The menu in English. Nothing extra is installed.\n\nYou can change this "
               "later on the Setup page in game.",
               type_="Recommended"),
        plugin("简体中文 (Simplified Chinese)",
               "安装设置菜单的简体中文翻译。\n\n请注意：这里翻译的只有本模组的菜单。物品名和 NPC 名来自游戏本身，"
               "所以如果你玩的是中文版天际，还需要在下一页勾选字体，否则那些名字会显示成一串问号。\n\n"
               "装好之后随时可以在游戏里的“设置”页切回英文。\n\n--\nInstalls the Simplified Chinese "
               "translation of the settings menu. This translates the menu only; item and NPC "
               "names come from the game, so a Chinese copy of Skyrim also needs the font on "
               "the last page.",
               "01 Chinese menu", flags={"chinese": "on"}),
    ])

    font = group("Font for non-Latin item names / 非拉丁文字体", "SelectAny", [
        plugin("中文字体 Noto Sans SC (Chinese font)",
               "名称标签用的是 SKSE Menu Framework 载入的字体。字体里没有的字会画成一个问号——"
               "中文、日文、韩文、俄文、泰文版的天际都会碰到这个问题。\n\n"
               "这一项会安装 Noto Sans SC 并命名为 MainFont.ttf，覆盖拉丁字母和 6763 个 GB2312 汉字。\n\n"
               "装完还必须自己去改 SKSE\\Plugins\\SKSEMenuFramework.ini 里的这两行，否则不会有任何变化：\n\n"
               "    PrimaryFont   = MainFont.ttf\n    EnableChinese = true\n\n"
               "字体采用 SIL Open Font License 1.1 授权，许可证原文 OFL.txt 会一起装上。\n"
               "体积 2.2 MB。英文版天际不需要这一项。\n\n--\nInstalls Noto Sans SC as MainFont.ttf: "
               "Latin plus 6,763 GB2312 hanzi. You must also set PrimaryFont and EnableChinese in "
               "SKSEMenuFramework.ini yourself. SIL Open Font License 1.1; OFL.txt is installed "
               "alongside it.",
               "02 CJK font", dep=("chinese", "on", "Recommended")),
    ])

    addons = group("Add-ons that install files", "SelectAny", [
        plugin("OStim Standalone",
               "Puts a Dibella sigil over people you have been with, and counts how many "
               "different things you have done together as a filling heart. It reads "
               "OStim's own records, writes nothing, and needs no patch.\n\nLeave this out "
               "and the add-on does not exist: no marker, no counter, and nothing about "
               "OStim anywhere in the menu.\n\nOnly install this if you have OStim "
               "Standalone.",
               "03 OStim add-on"),
    ])

    builtin = group("Add-ons already built in - nothing to install", "SelectAny", [
        plugin("SLO Aroused NG - already included",
               "Nothing to tick. This add-on is part of the plugin itself, so it is always "
               "present and there is no file to install or leave out.\n\nIf you have SLO "
               "Aroused NG, a small flame appears on the right of a person's name showing "
               "their arousal, and the add-on shows as \"installed\" on the Add-ons page. "
               "If you do not have it, the add-on reports itself as not installed and is "
               "hidden from the menu by default.\n\nNothing is required either way.",
               type_="NotUsable"),
        plugin("Floating subtitles - already included",
               "Nothing to tick. Also part of the plugin itself.\n\nIf you run a mod that "
               "floats dialogue subtitles over the speaker's head, this hides a person's "
               "name tag while they are talking so the two do not overlap or say the same "
               "thing twice. Set it to hide or lift on the Add-ons page.\n\nWorks with any "
               "subtitle mod; there is nothing to detect and nothing to install.",
               type_="NotUsable"),
    ])

    start = group("How would you like to begin", "SelectExactlyOne", [
        plugin("Preset: the sense",
               "Press a key and the world tells you what is worth taking: loot lights up "
               "in an outward wave, names and titles float over things and people, the "
               "colour drains while it runs.\n\nYour HUD and your combat mods stay "
               "exactly as they are. Everything remains adjustable in the menu.",
               "05 Preset - sense", type_="Recommended",
               image="fomod/images/preset-sense.png"),
        plugin("Preset: the sense, plus a sense-built combat HUD",
               "As above, and the mod becomes your combat readout too: enemies you fight "
               "carry health, magicka and stamina over their heads, your own bars follow "
               "you in third person, and TrueHUD and the vanilla enemy bar step aside so "
               "this is the only place that information appears.\n\nThe next page asks "
               "what to do with the rest of the game's interface.",
               "06 Preset - sense HUD", flags={"senseui": "on"},
               image="fomod/images/preset-sense-hud.png"),
        plugin("Set every option myself",
               "The full installer, page by page. The presets above are only bundles of "
               "the same answers - anything they choose, you can choose here.",
               flags={"custom": "on"}),
    ])

    hidehud = group("The rest of the game's interface", "SelectExactlyOne", [
        plugin("Keep it",
               "The game's own HUD - compass, crosshair, your bars - stays as it is. "
               "Only the enemy bars are taken over.",
               type_="Recommended"),
        plugin("Hide it - the sense is the interface",
               "The vanilla HUD goes too. Sweeps, the corner readout and the over-head "
               "bars become the only interface; the hidden menus come back the moment "
               "you untick it on the Setup page.",
               "07 Preset - hide HUD"),
    ])

    sweepbars = group("Vitals bars during a sweep", "SelectExactlyOne", [
        plugin("None",
               "A sweep names people and outlines them; nobody gets a health bar over "
               "their head.\n\nThe default: bars are for the two options below, or for a "
               "combat HUD mod.",
               type_="Recommended"),
        plugin("Over hostiles the wave reaches",
               "Everyone hostile to you that a sweep lights carries their bars for the "
               "length of the sweep - through walls, across the whole radius. A sweep "
               "through a bandit camp tells you what you are walking into.",
               "50 Sweep bars - hostile"),
        plugin("Over everyone lit",
               "As above, but friend and stranger too, not just enemies.",
               "51 Sweep bars - everyone"),
    ])

    combat = group("Bars over people you fight", "SelectExactlyOne", [
        plugin("Off",
               "No bars in combat beyond what a sweep shows.\n\nThe default, and the "
               "right answer if you run TrueHUD - its recent-damage bars already cover "
               "this ground.",
               type_="Recommended"),
        plugin("On - from your first hit to the end of the fight",
               "Anyone you have struck keeps bars over their head until the fight ends, "
               "no sweep needed. The dead keep a drained bar a few seconds so the kill "
               "reads. Health only by default; magicka and stamina are a switch in the "
               "menu.",
               "52 Combat bars"),
    ])

    overhead = group("Bars over your own head", "SelectExactlyOne", [
        plugin("Off",
               "Your own bars appear on your sweep tag and, if you turn it on in the "
               "menu, pinned to a screen corner.\n\nThe default.",
               type_="Recommended"),
        plugin("On - third person, no sweep needed",
               "The same stack the combat bars use, following you in third person "
               "whether or not anything else is happening. Shows a few seconds after a "
               "value moves; the rule is changeable in the menu. First person is what "
               "the corner readout is for.",
               "53 My overhead bars"),
    ])

    survival = group("When a survival mod lowers your maximum", "SelectExactlyOne", [
        plugin("Show it, dressed in frost and smoke",
               "The bar keeps its full length and the span a survival mod has taken "
               "becomes a dead zone: frost creeps over lost health, lost magicka and "
               "stamina smoulder in their own colour.\n\nHow the mod is meant to look. "
               "Harmless without a survival mod - the zone simply never appears.",
               type_="Recommended"),
        plugin("Show it plainly",
               "The same dead zone, undressed: just a dimmed span at the end of the "
               "bar.",
               "54 Survival - plain"),
        plugin("Do not show it",
               "Bars rescale to the reduced maximum and say nothing about it.",
               "55 Survival - off"),
    ])

    chime = group("Sweep chime", "SelectExactlyOne", [
        plugin("A quiet two-note chime",
               "A short chime when a sweep starts, played through the game's UI audio "
               "so its volume sliders apply. The sound is one wav on disk - replace it "
               "with any wav to make it yours; a note beside it says how.\n\nVolume and "
               "the switch live in the menu.",
               type_="Recommended"),
        plugin("Silence",
               "Sweeps start without a sound. The wav still installs, so you can turn "
               "the chime on later from the menu.",
               "56 Chime - off"),
    ])

    xml = ('<?xml version="1.0" encoding="utf-8"?>\n'
           '<config xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"\n'
           '        xsi:noNamespaceSchemaLocation="http://qconsulting.ca/fo3/ModConfig5.0.xsd">\n\n'
           "  <moduleName>Scavenger Sense</moduleName>\n\n"
           "  <requiredInstallFiles>\n"
           '    <folder source="00 Core" destination="" priority="0" />\n'
           "  </requiredInstallFiles>\n\n"
           '  <installSteps order="Explicit">\n\n')
    xml += step("Scavenger Sense / 快速开始", [start, keys]) + "\n"
    xml += step("Sense-built HUD / 界面接管", [hidehud], when=("senseui", "on")) + "\n"
    xml += step("How it should look", [tags, effect, lights, people], when=("custom", "on")) + "\n"
    xml += step("Vitals bars and sound / 状态条与提示音",
        [sweepbars, combat, overhead, survival, chime], when=("custom", "on")) + "\n"
    xml += step("Menu language / 菜单语言", [language]) + "\n"
    xml += step("Optional extras / 可选内容", [font, addons, builtin])
    xml += "\n  </installSteps>\n\n</config>\n"
    return xml


if __name__ == "__main__":
    # clear out any payload folder we own, so a removed answer cannot linger
    for d in FRAGMENTS:
        p = os.path.join(BUILD, d)
        if os.path.isdir(p):
            shutil.rmtree(p)
    sync_payload()
    write_payload()
    open(os.path.join(BUILD, "fomod/ModuleConfig.xml"), "w", encoding="utf-8").write(build_xml())
    n = sum(len(fs) for _, _, fs in os.walk(BUILD))
    print(f"payload folders: {len([d for d in os.listdir(BUILD) if os.path.isdir(os.path.join(BUILD, d)) and d != 'fomod'])}")
    print(f"files in tree  : {n}")
