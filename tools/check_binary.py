"""Inspect a PE without loading it; optionally check every SMF import against a host DLL."""
import argparse
import json
from pathlib import Path
import re
import struct


class PE:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        nt = self.unpack('<I', 60)[0]
        assert self.data[nt:nt + 4] == b'PE\0\0', 'not a PE image'
        self.machine, count = self.unpack('<HH', nt + 4)
        optional_size = self.unpack('<H', nt + 20)[0]
        self.optional = nt + 24
        assert self.unpack('<H', self.optional)[0] == 0x20B, 'not PE32+'
        self.sections = []
        for i in range(count):
            size, address, raw_size, raw = self.unpack('<IIII', self.optional + optional_size + 40 * i + 8)
            self.sections.append((address, max(size, raw_size), raw))

    def unpack(self, fmt, at): return struct.unpack_from(fmt, self.data, at)
    def offset(self, rva):
        for address, size, raw in self.sections:
            if address <= rva < address + size: return raw + rva - address
        return rva  # image headers
    def string(self, rva):
        at = self.offset(rva)
        return self.data[at:self.data.index(0, at)].decode('ascii')
    def directory(self, index): return self.unpack('<II', self.optional + 112 + 8 * index)
    def exports(self):
        rva, _ = self.directory(0)
        if not rva: return {}
        at = self.offset(rva)
        count, functions, names, ordinals = self.unpack('<IIII', at + 24)
        result = {}
        for i in range(count):
            name = self.string(self.unpack('<I', self.offset(names) + 4 * i)[0])
            ordinal = self.unpack('<H', self.offset(ordinals) + 2 * i)[0]
            result[name] = self.unpack('<I', self.offset(functions) + 4 * ordinal)[0]
        return result
    def imports(self):
        rva, _ = self.directory(1)
        result = {}
        if not rva: return result
        at = self.offset(rva)
        while True:
            original, _, _, name, first = self.unpack('<IIIII', at)
            if not name: break
            functions = []
            thunk = self.offset(original or first)
            while (value := self.unpack('<Q', thunk)[0]):
                functions.append('#' + str(value & 0xFFFF) if value >> 63 else self.string(value + 2))
                thunk += 8
            result[self.string(name)] = functions
            at += 20
        return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dll')
    parser.add_argument('--framework')
    parser.add_argument('--version', help='Expected four-part plugin version (default: main plugin source)')
    args = parser.parse_args()
    plugin = PE(args.dll)
    assert plugin.machine == 0x8664, 'plugin must be x64'
    exports = plugin.exports()
    assert {'SKSEPlugin_Load', 'SKSEPlugin_Version'} <= exports.keys(), 'missing SKSE exports'
    root = Path(__file__).resolve().parent.parent
    version = tuple(map(int,args.version.split('.'))) if args.version else tuple(map(int, re.search(r'kVersion\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)',
                                     (root / 'plugin/src/Main.cpp').read_text()).groups()))
    assert len(version)==4, 'version must have four parts'
    expected = version[0] << 24 | version[1] << 16 | version[2] << 4 | version[3]
    metadata = plugin.unpack('<II', plugin.offset(exports['SKSEPlugin_Version']))
    assert metadata == (1, expected), f'wrong compiled plugin version: {metadata}'
    imports = plugin.imports()
    assert not any(name.lower().endswith('d.dll') and name.lower().startswith(('msvcp', 'vcruntime', 'ucrtbase')) for name in imports), 'debug CRT dependency'
    host_imports = next((symbols for name, symbols in imports.items() if name.lower() == 'sksemenuframework.dll'), [])
    assert host_imports, 'framework import missing'
    if args.framework:
        host = PE(args.framework)
        available = host.exports()
        missing = set(host_imports) - available.keys()
        assert not missing, f'framework lacks imported symbols: {sorted(missing)}'
        assert 'RegisterHudElement' in available, 'host lacks HUD registration'
    print(json.dumps({'version': '.'.join(map(str, version)), 'x64': True,
                      'framework_imports_checked': len(host_imports) if args.framework else 0,
                      'imports': sorted(imports)}, indent=2))
