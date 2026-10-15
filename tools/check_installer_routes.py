"""Exercise both installer languages, presets, quick controls and each detailed answer."""
from pathlib import Path
import argparse
import json
import math
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / 'fomod-build'
config = ET.parse(BUILD / 'fomod/ModuleConfig.xml').getroot()
all_steps = list(config.find('installSteps'))
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--probe', help='Run the production ConfigProbe.exe loader in isolated Data trees')
parser.add_argument('--report', help='Write check counts as JSON')
args = parser.parse_args()
probe = str(Path(args.probe).resolve()) if args.probe else None
native_loads = 0


def ini_values(path):
    values, section = {}, ''
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        line = line.split(';')[0].strip()
        if line.startswith('['): section = line.strip('[]').lower()
        elif '=' in line:
            key, value = line.split('=', 1)
            values[section + '.' + key.strip().lower()] = value.strip().lower()
    return values


required_preset_keys = {'effects.screenenabled', 'effects.screenstrength',
                        'vision.glowenabled', 'labels.smoothingactorsonly'}
for name in ('KShakes Choice.ini', 'Only Sensing.ini', 'Sensing and Tracking.ini'):
    values = ini_values(ROOT / 'mod/SKSE/Plugins/ScavengerSense/presets' / name)
    assert required_preset_keys <= values.keys(), f'{name} omits the current display settings'


def equal_value(actual, expected):
    try: return math.isclose(float(actual), float(expected), rel_tol=1e-5, abs_tol=1e-5)
    except ValueError: return actual.casefold() == expected.casefold()


def check_native(sources, settings, preset, controls):
    global native_loads
    with tempfile.TemporaryDirectory(prefix='scavenger-installer-') as temp:
        data = Path(temp) / 'Data'
        for source in sources:
            folder = BUILD / source
            for file in folder.rglob('*.ini'):
                relative = file.relative_to(folder)
                if 'scavengersense_setup' not in relative.as_posix().lower(): continue
                target = data / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(file, target)
        def run(*arguments):
            global native_loads
            result = subprocess.run([probe, *map(str, arguments)], cwd=temp, check=True, capture_output=True, text=True)
            native_loads += 1
            return json.loads(result.stdout)
        native = run()
        for key, expected in settings.items():
            if key in native:
                assert equal_value(native[key], expected), f'production loader differs: {key}: {native[key]} != {expected}'
        assert native['labels.smoothingactorsonly'] == 'true'
        assert native['vision.glowenabled'] == 'true'
        if preset < 3:
            name = ('KShakes Choice.ini', 'Only Sensing.ini', 'Sensing and Tracking.ini')[preset]
            loaded = run('--preset', ROOT / 'mod/SKSE/Plugins/ScavengerSense/presets' / name)
            excluded = {'general.language'}
            if controls:
                excluded |= {'hotkey.trigger', 'hotkey.holdtime', 'tracks.keymode'}
            for key, value in loaded.items():
                if key not in excluded:
                    assert equal_value(native[key], value), f'{name}: installer/menu differ for {key}'
        # A real personal INI must win, including new keys and old gesture keys.
        own = data / 'SKSE/Plugins/ScavengerSense.ini'
        own.parent.mkdir(parents=True, exist_ok=True)
        own.write_text('[Effects]\nscreenEnabled=false\nscreenStrength=.25\n[Labels]\nsmoothingActorsOnly=false\n'
                       '[Vision]\nglowEnabled=false\n[Hotkey]\ntrigger=hold\n', encoding='utf-8')
        personal = run()
        assert personal['effects.screenenabled'] == personal['labels.smoothingactorsonly'] == personal['vision.glowenabled'] == 'false'
        assert float(personal['effects.screenstrength']) == .25 and personal['hotkey.trigger'] == 'hold'


def visible(element, flags):
    if element is None: return True
    dependencies = list(element.iter('flagDependency'))
    return all(flags.get(x.get('flag')) == x.get('value') for x in dependencies)


def simulate(language, preset, controls, override=None):
    flags, sources, visited = {}, ['00 Core'], []
    groups_seen = 0
    for step in all_steps:
        if not visible(step.find('visible'), flags): continue
        visited.append(step)
        for group in step.findall('optionalFileGroups/group'):
            plugins = list(group.find('plugins'))
            if step is all_steps[0]:
                selected = [plugins[language]]
            elif groups_seen == 0:
                selected = [plugins[preset]]
            elif 'Sense activation' == group.get('name') or '感知触发方式' == group.get('name'):
                selected = [plugins[controls]]
            elif override and group.get('name') == override[0]:
                selected = [plugins[override[1]]]
            elif group.get('type') == 'SelectExactlyOne':
                selected = [next((p for p in plugins if p.find('typeDescriptor/type') is not None
                                 and p.find('typeDescriptor/type').get('name') == 'Recommended'), plugins[0])]
            else:
                selected = []
            for plugin in selected:
                for flag in plugin.findall('conditionFlags/flag'): flags[flag.get('name')] = flag.text
                sources.extend(f.get('source') for f in plugin.findall('files/folder'))
            if step is not all_steps[0]: groups_seen += 1
    assert len(sources) == len(set(sources)), f'duplicate installs: {sources}'
    destinations = {}
    for source in sources:
        folder = BUILD / source
        for file in folder.rglob('*'):
            if not file.is_file(): continue
            destination = file.relative_to(folder).as_posix().lower()
            assert destination not in destinations, f'colliding selected payloads: {destination}'
            destinations[destination] = file
    settings = {}
    for key, path in sorted(destinations.items()):
        if 'scavengersense_setup/' not in key: continue
        section = ''
        for line in path.read_text(encoding='utf-8-sig').splitlines():
            line = line.split(';')[0].strip()
            if line.startswith('['): section = line.strip('[]').lower()
            elif '=' in line:
                key_, value = line.split('=', 1)
                settings[section + '.' + key_.strip().lower()] = value.strip().lower()
    assert settings['general.language'] == ('english', 'chinese')[language]
    if controls:
        assert settings['hotkey.trigger'] == ('', 'press', 'hold')[controls]
        assert settings['tracks.keymode'] == 'single'
    assert len(visited) == (7 if preset == 3 else 4), f'wrong pages: {[s.get("name") for s in visited]}'
    guides = [g for step in visited for g in step.findall('optionalFileGroups/group')
              if g.get('name') in ('Display settings and upgrades', '显示设置与升级')]
    assert len(guides) == 1 and len(guides[0].findall('plugins/plugin')) == 2, 'all presets need the new display/upgrade guide'
    if preset == 3:
        # Named installer choices must explicitly override current defaults.
        contracts = {
            'People': ('categories.actor', ('false', 'true', 'true')),
            '人物': ('categories.actor', ('false', 'true', 'true')),
            'Screen effect while a sweep runs': ('effects.screenenabled', ('true', 'true', 'true', 'false')),
            '感知期间的画面效果': ('effects.screenenabled', ('true', 'true', 'true', 'false')),
            'Sweep chime': ('sound.enabled', ('true', 'false')),
            '感知提示音': ('sound.enabled', ('true', 'false')),
        }
        for name, (key, expected) in contracts.items():
            if override and override[0] == name:
                assert settings.get(key) == expected[override[1]], f'{name} choice has no effective setting'
        if override and override[0] in ('Bars over people you fight', '战斗目标状态条') and override[1] == 1:
            assert settings.get('vitals.combatwhen') == 'struck', 'first-hit option must use struck mode'
    if language == 0:
        for step in visited[1:]:
            text = ET.tostring(step, encoding='unicode')
            assert not any('\u4e00' <= c <= '\u9fff' for c in text), 'Chinese text leaked into English route'
    if probe: check_native(sources, settings, preset, controls)
    return sources


checks = 0
for language in (0, 1):
    for preset in range(4):
        for controls in range(3):
            simulate(language, preset, controls); checks += 1
    language_name = ('english', 'chinese')[language]
    for step in all_steps[1:]:
        dependencies = {x.get('flag'): x.get('value') for x in step.findall('visible//flagDependency')}
        if dependencies.get('installerLanguage') != language_name: continue
        if step.get('name') in ('Scavenger Sense', 'Scavenger Sense — 快速开始', 'Controls', '按键与触发方式'): continue
        for group in step.findall('optionalFileGroups/group'):
            for index, option in enumerate(group.find('plugins')):
                sources = simulate(language, 3, 0, (group.get('name'), index))
                for folder in option.findall('files/folder'): assert folder.get('source') in sources
                checks += 1
scope = 'preset/menu parity' if probe else 'XML and fragment contracts'
print(f'{checks} installer routes passed; {scope} and guides checked; {native_loads} production config loads; no selected-file collisions')
if args.report:
    Path(args.report).write_text(json.dumps({'installer_routes':checks, 'production_config_loads':native_loads,
                                          'native_probe_used':bool(probe), 'preset_menu_parity_checked':bool(probe), 'preset_guide_and_choice_contracts':True}, indent=2)+'\n', encoding='utf-8')
