"""Check required Unicode glyphs in a TrueType/OpenType font using its cmap."""
import argparse
import json
from pathlib import Path
import struct


def contains(data, cmap, point):
    u16 = lambda at: struct.unpack_from('>H', data, at)[0]
    u32 = lambda at: struct.unpack_from('>I', data, at)[0]
    for i in range(u16(cmap + 2)):
        platform, encoding = struct.unpack_from('>HH', data, cmap + 4 + i * 8)
        if platform != 0 and not (platform == 3 and encoding in (1, 10)): continue
        sub = cmap + u32(cmap + 4 + i * 8 + 4)
        kind = u16(sub)
        if kind == 12:
            for group in range(u32(sub + 12)):
                first, last, glyph = struct.unpack_from('>III', data, sub + 16 + group * 12)
                if first <= point <= last and glyph + point - first: return True
        elif kind == 4 and point <= 0xFFFF:
            count = u16(sub + 6) // 2
            ends = sub + 14
            starts, deltas, ranges = ends + count * 2 + 2, ends + count * 4 + 2, ends + count * 6 + 2
            for segment in range(count):
                if u16(starts + 2 * segment) <= point <= u16(ends + 2 * segment):
                    distance = u16(ranges + 2 * segment)
                    glyph = (point + u16(deltas + 2 * segment)) & 0xFFFF
                    if distance:
                        glyph = u16(ranges + 2 * segment + distance + 2 * (point - u16(starts + 2 * segment)))
                        if glyph: glyph = (glyph + u16(deltas + 2 * segment)) & 0xFFFF
                    if glyph: return True
    return False


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('font')
    parser.add_argument('--text', default='中文设置玩家生命魔法耐力罗盘字幕消息ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789')
    parser.add_argument('--text-file', help='UTF-8 text to check instead of --text')
    parser.add_argument('--cjk-only', action='store_true', help='Check CJK ideographs only')
    args = parser.parse_args()
    text = Path(args.text_file).read_text(encoding='utf-8-sig') if args.text_file else args.text
    characters = {c for c in text if c.isprintable()}
    if args.cjk_only:
        characters = {c for c in characters if 0x3400 <= ord(c) <= 0x9FFF}
    data = Path(args.font).read_bytes()
    cmap = None
    for i in range(struct.unpack_from('>H', data, 4)[0]):
        name, _, offset, _ = struct.unpack_from('>4sIII', data, 12 + 16 * i)
        if name == b'cmap': cmap = offset
    assert cmap is not None, 'font has no Unicode character map'
    missing = [c for c in sorted(characters) if not contains(data, cmap, ord(c))]
    print(json.dumps({'font': args.font, 'text_file': args.text_file,
                      'cjk_only': args.cjk_only, 'characters_checked': len(characters),
                      'missing': missing}, ensure_ascii=False))
    assert not missing, f'missing glyphs: {missing}'
