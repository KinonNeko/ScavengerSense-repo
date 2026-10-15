"""Compare compiled header layouts with the host's own check, without loading its DLL.

Understands the MSVC x64 leaf implementations in the verified ImGui 1.90.8
framework. Unknown code shapes fail inspection rather than assuming a match.
"""
import argparse
import json
import re
import struct
import subprocess
try:
    from .check_binary import PE
except ImportError:
    from check_binary import PE


def body(pe, rva):
    for _ in range(8):
        at = pe.offset(rva)
        if pe.data[at] == 0xE9:
            rva += 5 + pe.unpack('<i', at + 1)[0]
        else:
            return rva, pe.data[at:at + 160]
    raise ValueError('unexpected export jump chain')


def inspect(path):
    pe = PE(path)
    exports = pe.exports()
    rva, code = body(pe, exports['igGetVersion'])
    assert code[:3] == b'\x48\x8d\x05' and code[7] == 0xC3, 'unknown version getter'
    version = pe.string(rva + 7 + struct.unpack_from('<i', code, 3)[0])
    _, check = body(pe, exports['igDebugCheckVersionAndDataLayout'])
    result = {'version': version}
    for name, opcode, width in (
        ('io', b'\x48\x81\xfa', 4), ('style', b'\x49\x81\xf8', 4),
        ('vec2', b'\x49\x83\xf9', 1), ('vec4', b'\x48\x83\x7c\x24\x28', 1),
        ('drawvert', b'\x48\x83\x7c\x24\x30', 1), ('drawidx', b'\x48\x83\x7c\x24\x38', 1),
    ):
        matches = list(re.finditer(re.escape(opcode), check))
        assert len(matches) == 1, f'unknown {name} comparison'
        at = matches[0].end()
        result[name] = int.from_bytes(check[at:at + width], 'little')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('framework')
    parser.add_argument('--probe', required=True, help='our compiled FrameworkAbiProbe executable')
    args = parser.parse_args()
    host = inspect(args.framework)
    run = subprocess.run([args.probe], check=True, capture_output=True, text=True)
    compiled = json.loads(run.stdout)
    assert host == compiled, f'ImGui layout mismatch: host {host}; compiled {compiled}'
    print(json.dumps({'host': host, 'compiled': compiled, 'matches': True}, indent=2))
