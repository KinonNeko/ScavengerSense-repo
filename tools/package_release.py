"""Package the generated FOMOD, verifying versions, CRCs and every payload byte."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET
import zipfile

from check_binary import PE

ROOT = Path(__file__).resolve().parent.parent


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def package(output):
    tree = ROOT / 'fomod-build'
    main = (ROOT / 'plugin/src/Main.cpp').read_text(encoding='utf-8')
    version = tuple(map(int, re.search(r'kVersion\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)', main).groups()))
    short = '.'.join(map(str, version[:3]))
    cmake = (ROOT / 'plugin/CMakeLists.txt').read_text(encoding='utf-8')
    assert re.search(r'project\(ScavengerSense VERSION (\S+)', cmake).group(1) == short
    for path in (ROOT / 'fomod/info.xml', tree / 'fomod/info.xml'):
        info = ET.parse(path).find('Version')
        assert info.get('MachineVersion') == '.'.join(map(str, version))
        assert info.text == short + ' beta'
    dll = tree / '00 Core/SKSE/Plugins/ScavengerSense.dll'
    candidates = [ROOT / 'plugin/build/Release/ScavengerSense.dll', ROOT / 'plugin/build/ScavengerSense.dll']
    built = next(path for path in candidates if path.is_file())
    assert dll.read_bytes() == built.read_bytes(), 'stale DLL in generated tree'
    pe = PE(dll)
    encoded = version[0] << 24 | version[1] << 16 | version[2] << 4 | version[3]
    assert pe.unpack('<II', pe.offset(pe.exports()['SKSEPlugin_Version'])) == (1, encoded)
    assert pe.machine == 0x8664
    capture = tree / '04 Screenshot add-on/SKSE/Plugins/ScavengerSenseCapture.dll'
    if capture.is_file():
        compiled = next(ROOT / folder / 'ScavengerSenseCapture.dll' for folder in
                        ('plugin/build/Release','plugin/build') if (ROOT / folder / 'ScavengerSenseCapture.dll').is_file())
        assert capture.read_bytes() == compiled.read_bytes(), 'stale capture DLL'
        addon = PE(capture)
        assert addon.machine == 0x8664 and 'ScavengerSenseCapture_Request' in addon.exports()
        assert addon.unpack('<II', addon.offset(addon.exports()['SKSEPlugin_Version'])) == (1, 1 << 16)
        assert 'scavengersensecapture.dll' not in {name.lower() for name in pe.imports()}, 'core must not require optional DLL'
    assert (tree / '00 Core/ScavengerSense.esp').read_bytes() == (ROOT / 'ScavengerSense.esp').read_bytes()
    files = {path.relative_to(tree).as_posix(): path for path in sorted(tree.rglob('*')) if path.is_file()}
    assert 'fomod/ModuleConfig.xml' in files and not any(name.startswith('Data/') for name in files)
    assert not any(name.endswith(('.pdb', '.log', '.exe')) for name in files), 'development files in payload'
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    # Refuse to replace an earlier download. Back it up explicitly if needed.
    with zipfile.ZipFile(output, 'x', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, path in files.items():
            archive.write(path, name)
    with zipfile.ZipFile(output) as archive:
        assert archive.testzip() is None, 'CRC failure'
        assert len(archive.namelist()) == len(set(archive.namelist())) == len(files)
        assert set(archive.namelist()) == set(files)
        for name, path in files.items():
            assert archive.read(name) == path.read_bytes(), f'payload differs: {name}'
    return {'version': short + ' beta', 'archive': str(output), 'bytes': output.stat().st_size,
            'members': len(files), 'sha256': sha256(output), 'dll_sha256': sha256(dll),
            'esp_sha256': sha256(ROOT / 'ScavengerSense.esp'),
            'payload': {name: sha256(path) for name, path in files.items()}}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = package(args.output)
    if args.report:
        args.report.write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(json.dumps({key: value for key, value in result.items() if key != 'payload'}, indent=2, ensure_ascii=False))
