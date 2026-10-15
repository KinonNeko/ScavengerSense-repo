"""Rasterise DisplayChecks' simple SVG draw-call fixture for visual inspection.

Requires Pillow for this developer-only preview, not for the plugin/build.
The fixture uses production glyph geometry; it is not a gameplay screenshot.
"""
import argparse
from pathlib import Path
import xml.etree.ElementTree as ET
from PIL import Image, ImageDraw, ImageFont


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('svg')
    parser.add_argument('png')
    args = parser.parse_args()
    root = ET.parse(args.svg).getroot()
    scale = 2
    canvas = Image.new('RGB', (int(root.attrib['width']) * scale, int(root.attrib['height']) * scale))
    draw = ImageDraw.Draw(canvas)
    font_path = Path('C:/Windows/Fonts/arial.ttf')
    for el in root:
        tag = el.tag.rsplit('}', 1)[-1]
        a = el.attrib
        fill = a.get('fill')
        if fill == 'none': fill = None
        stroke = a.get('stroke')
        width = max(1, round(float(a.get('stroke-width', 1)) * scale))
        if tag == 'rect':
            x, y = float(a.get('x', 0)), float(a.get('y', 0))
            draw.rectangle((x*scale, y*scale, (x+float(a['width']))*scale, (y+float(a['height']))*scale), fill=fill)
        elif tag == 'circle':
            x, y, r = (float(a[key]) for key in ('cx', 'cy', 'r'))
            draw.ellipse(((x-r)*scale, (y-r)*scale, (x+r)*scale, (y+r)*scale), fill=fill, outline=stroke, width=width)
        elif tag in ('polygon', 'polyline'):
            points = [tuple(float(c)*scale for c in pair.split(',')) for pair in a['points'].split()]
            if fill: draw.polygon(points, fill=fill)
            if stroke:
                draw.line(points + points[:1] if tag == 'polygon' else points, fill=stroke, width=width, joint='curve')
        elif tag == 'text':
            font = ImageFont.truetype(str(font_path), round(float(a['font-size'])*scale))
            draw.text((float(a['x'])*scale, float(a['y'])*scale), el.text or '', fill=fill, font=font, anchor='ls')
        else:
            raise ValueError(f'Unsupported fixture primitive: {tag}')
    canvas.save(args.png)
    print(args.png)
