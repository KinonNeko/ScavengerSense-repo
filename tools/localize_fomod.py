"""Generate mutually exclusive English and Chinese installer pages.

Translation copy lives in fomod/installer_chinese.json and is reused on every
build. A new option without a Chinese entry fails the build instead of quietly
shipping English on the Chinese route.
"""
import copy
import json
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent.parent


def localize(xml):
    root = ET.fromstring(xml)
    steps = root.find('installSteps')
    original = list(steps)
    steps.clear()
    steps.set('order', 'Explicit')
    selector = ET.SubElement(steps, 'installStep', name='Language / 语言')
    groups = ET.SubElement(selector, 'optionalFileGroups', order='Explicit')
    group = ET.SubElement(groups, 'group', name='Installer and menu language / 安装器与菜单语言', type='SelectExactlyOne')
    plugins = ET.SubElement(group, 'plugins', order='Explicit')
    for code, name, desc, folder in (
        ('english', 'English', 'Use English for the installer and the in-game menu. Existing saved settings take precedence.', '01 English menu'),
        ('chinese', '简体中文', '使用中文安装说明，并为全新安装设置中文游戏菜单。已有的个人 INI 设置优先；可在游戏菜单里修改语言。', '01 Chinese menu'),
    ):
        plugin = ET.SubElement(plugins, 'plugin', name=name)
        ET.SubElement(plugin, 'description').text = desc
        files = ET.SubElement(plugin, 'files')
        ET.SubElement(files, 'folder', source=folder, destination='', priority='0')
        flags = ET.SubElement(plugin, 'conditionFlags')
        ET.SubElement(flags, 'flag', name='installerLanguage').text = code
        ET.SubElement(flags, 'flag', name='chinese').text = 'on' if code == 'chinese' else 'off'
        types = ET.SubElement(plugin, 'typeDescriptor')
        ET.SubElement(types, 'type', name='Recommended' if code == 'english' else 'Optional')

    catalogue = json.loads((ROOT / 'fomod/installer_chinese.json').read_text(encoding='utf-8'))
    for code in ('english', 'chinese'):
        for source in original:
            step = copy.deepcopy(source)
            # Scope answer flags to their language route. Toggling the first
            # page cannot make a hidden route's 'custom' flag enable this one.
            for flag in step.iter('flag'):
                if flag.get('name') == 'custom': flag.set('name', f'custom_{code}')
            for dep in step.iter('flagDependency'):
                if dep.get('flag') == 'custom': dep.set('flag', f'custom_{code}')
            visible = step.find('visible')
            if visible is None:
                visible = ET.Element('visible')
                step.insert(0, visible)
            visible.set('operator', 'And')
            ET.SubElement(visible, 'flagDependency', flag='installerLanguage', value=code)
            if code == 'chinese':
                step.set('name', catalogue['names'][step.get('name')])
                for group in step.iter('group'):
                    group_name = group.get('name')
                    group.set('name', catalogue['names'][group_name])
                    for plugin in group.findall('plugins/plugin'):
                        key = group_name + ' / ' + plugin.get('name')
                        entry = catalogue['options'][key]
                        plugin.set('name', entry['name'])
                        plugin.find('description').text = entry['description']
            else:
                step.set('name', step.get('name').split(' / ')[0])
                for group in step.iter('group'):
                    group.set('name', group.get('name').split(' / ')[0])
                    for plugin in group.findall('plugins/plugin'):
                        if plugin.get('name') == '中文字体 Noto Sans SC (Chinese font)':
                            plugin.set('name', 'Noto Sans SC font')
                            plugin.find('description').text = (
                                'Installs Noto Sans SC as MainFont.ttf: Latin and 6,763 GB2312 Chinese characters. '
                                'This does not guarantee coverage for Japanese, Korean, Russian or Thai.\n\n'
                                'In SKSE/Plugins/SKSEMenuFramework.ini set:\n'
                                'PrimaryFont = MainFont.ttf\nEnableChinese = true\n\n'
                                'Your framework settings are not changed automatically. Other languages need '
                                'a suitable font and the matching glyph ranges. SIL Open Font License 1.1; OFL.txt included.')
                # The source used to mix Chinese notes into English copy.
                for description in step.iter('description'):
                    text = description.text or ''
                    if '\n\n--\n\n请先阅读' in text:
                        description.text = text.split('\n\n--\n\n请先阅读')[0]
            steps.append(step)
    ET.indent(root, space='  ')
    return '<?xml version="1.0" encoding="utf-8"?>\n' + ET.tostring(root, encoding='unicode') + '\n'
