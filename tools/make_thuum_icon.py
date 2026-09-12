#!/usr/bin/env python3
"""Draw the Thu'um glyph - a dragon-script rune of claw-cut wedges, the way
the alphabet is carved into a word wall - as the 128x128 white-on-transparent
PNG the stats row tints and fills at draw time.

Same method as make_dibella_icon.py: shapes tested per supersampled pixel,
PNG written by hand. Run from the repo root; overwrites
mod/SKSE/Plugins/ScavengerSense/icons/thuum.png. Anyone who would rather
have the letters exactly as the game draws them can drop their own PNG over
it - the file is the whole contract.
"""
import math
import os
import struct
import sys
import zlib

SIZE = 128
SS = 4


def wedge(px, py, x0, y0, x1, y1, w0, w1):
    """A tapered stroke from (x0,y0) to (x1,y1): width w0 at the start, w1 at
    the end, square ends. The cut a claw leaves."""
    dx, dy = x1 - x0, y1 - y0
    length = math.hypot(dx, dy)
    if length < 1e-6:
        return False
    ux, uy = dx / length, dy / length
    t = (px - x0) * ux + (py - y0) * uy
    if t < 0.0 or t > length:
        return False
    d = abs((px - x0) * -uy + (py - y0) * ux)
    w = w0 + (w1 - w0) * (t / length)
    return d <= w * 0.5


def inside(x, y):
    # The tall stroke, top-heavy, biting down to a point.
    if wedge(x, y, 38, 14, 44, 114, 22, 5):
        return True
    # Three claws off its right flank, each shorter than the last.
    if wedge(x, y, 58, 22, 112, 36, 16, 4):
        return True
    if wedge(x, y, 58, 54, 104, 66, 14, 4):
        return True
    if wedge(x, y, 58, 86, 94, 98, 12, 3):
        return True
    return False


def render():
    rows = []
    for j in range(SIZE):
        row = bytearray()
        for i in range(SIZE):
            hits = 0
            for sj in range(SS):
                for si in range(SS):
                    if inside(i + (si + 0.5) / SS, j + (sj + 0.5) / SS):
                        hits += 1
            row += bytes((255, 255, 255, round(255 * hits / (SS * SS))))
        rows.append(bytes(row))
    return rows


def write_png(path, rows):
    def chunk(tag, data):
        body = tag + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body) & 0xFFFFFFFF)
    raw = b''.join(b'\x00' + r for r in rows)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', SIZE, SIZE, 8, 6, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        root, 'mod', 'SKSE', 'Plugins', 'ScavengerSense', 'icons', 'thuum.png')
    rows = render()
    write_png(out, rows)
    for j in range(0, SIZE, 4):
        print(''.join('#' if rows[j][i * 4 + 3] > 128 else ('+' if rows[j][i * 4 + 3] > 40 else '.')
                      for i in range(0, SIZE, 2)))
    print('wrote', out)


if __name__ == '__main__':
    main()
