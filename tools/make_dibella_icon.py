#!/usr/bin/env python3
"""Draw Dibella's sigil - a lily - as the 128x128 white-on-transparent PNG the
marker system tints at draw time.

No image library: the shape is a handful of lenses and circles, supersampled
into an alpha mask, and the PNG is written by hand with zlib. Run it from the
repo root; it overwrites mod/SKSE/Plugins/ScavengerSense/icons/dibella.png.

Why a lily: the goddess of beauty is drawn with a flower wherever the Divines
are drawn as sigils, and a bloom fills from the stem up, which is what the
tally needs from a picture.
"""
import math
import os
import struct
import sys
import zlib

SIZE = 128
SS = 4  # supersampling per axis


def lens(px, py, cx, cy, length, width, angle):
    """A leaf shape: the intersection of two circles. Upright at angle 0,
    with length tip to tip along its axis and width across the middle; a
    positive angle leans the top to the right. Returns True inside."""
    s, c = math.sin(angle), math.cos(angle)
    dx, dy = px - cx, py - cy
    # into the lens's own frame: u across, v along the axis (up is negative)
    u = dx * c + dy * s
    v = -dx * s + dy * c
    half = length / 2.0
    w = width / 2.0
    r = (half * half + w * w) / (2.0 * w)
    d = r - w
    return (u + d) ** 2 + v * v <= r * r and (u - d) ** 2 + v * v <= r * r


def disc(px, py, cx, cy, r):
    return (px - cx) ** 2 + (py - cy) ** 2 <= r * r


def bar(px, py, x0, y0, x1, y1):
    return x0 <= px <= x1 and y0 <= py <= y1


def inside(x, y):
    # y grows downward, like the image. The bloom sits high, the stem below.
    # Centre petal, upright and tallest.
    if lens(x, y, 64, 42, 62, 22, 0.0):
        return True
    # Two side petals leaning out from the same throat.
    for sign in (-1, 1):
        if lens(x, y, 64 + sign * 21, 54, 52, 18, sign * math.radians(46)):
            return True
    # The throat that joins them.
    if disc(x, y, 64, 76, 10):
        return True
    # Stem.
    if bar(x, y, 61, 76, 67, 114):
        return True
    # Two small leaves on the stem.
    if lens(x, y, 51, 98, 24, 9, math.radians(-50)):
        return True
    if lens(x, y, 77, 106, 24, 9, math.radians(50)):
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
                    x = i + (si + 0.5) / SS
                    y = j + (sj + 0.5) / SS
                    if inside(x, y):
                        hits += 1
            a = round(255 * hits / (SS * SS))
            row += bytes((255, 255, 255, a))
        rows.append(bytes(row))
    return rows


def write_png(path, rows):
    def chunk(tag, data):
        body = tag + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body) & 0xFFFFFFFF)
    raw = b''.join(b'\x00' + r for r in rows)
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', SIZE, SIZE, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 9))
    png += chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        root, 'mod', 'SKSE', 'Plugins', 'ScavengerSense', 'icons', 'dibella.png')
    rows = render()
    write_png(out, rows)
    # A glance at it in the terminal, so a broken shape is caught here.
    for j in range(0, SIZE, 4):
        print(''.join('#' if rows[j][i * 4 + 3] > 128 else ('+' if rows[j][i * 4 + 3] > 40 else '.')
                      for i in range(0, SIZE, 2)))
    print('wrote', out)


if __name__ == '__main__':
    main()
