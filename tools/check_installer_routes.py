"""Exercise both installer languages, presets, quick controls and each detailed answer."""
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / 'fomod-build'
config = ET.parse(BUILD / 'fomod/ModuleConfig.xml').getroot()
all_steps = list(config.find('installSteps'))


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
    if language == 0:
        for step in visited[1:]:
            text = ET.tostring(step, encoding='unicode')
            assert not any('\u4e00' <= c <= '\u9fff' for c in text), 'Chinese text leaked into English route'
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
print(f'{checks} installer routes passed; language and control fragments override presets; no selected-file collisions')
