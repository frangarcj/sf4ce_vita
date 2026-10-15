#!/usr/bin/env python3
"""Render original SVG sources to Vita-compatible indexed PNGs (CairoSVG/Pillow)."""
from pathlib import Path
import cairosvg
from PIL import Image

root = Path(__file__).resolve().parents[1]
outputs = {
    'icon': 'sce_sys/icon0.png',
    'livearea': 'sce_sys/livearea/contents/bg0.png',
    'startup': 'sce_sys/livearea/contents/startup.png',
    'config': 'sce_sys/livearea/contents/config.png',
}
for name, destination in outputs.items():
    target = root / destination
    cairosvg.svg2png(url=str(root / 'artwork' / f'{name}.svg'), write_to=str(target))
    with Image.open(target) as image:
        image.convert('RGB').quantize(colors=256).save(target)
