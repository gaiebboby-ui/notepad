#!/usr/bin/env python3
"""Regenerate res/Toolbar*.bmp from tools/images SVG/PNG sources."""

import os
import sys

try:
	import cairosvg
except ImportError:
	cairosvg = None

from ImageTool import make_notepad4_toolbar_bitmap

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
TOOLS = os.path.dirname(__file__)
SIZES = (16, 24, 32, 40, 48)


def render_preview_pngs() -> None:
	if cairosvg is None:
		print('cairosvg not installed; skip Preview.png regeneration')
		return
	for name in ('Preview', 'PreviewMaximize'):
		svg_path = os.path.join(TOOLS, 'images', f'{name}.svg')
		if not os.path.isfile(svg_path):
			raise FileNotFoundError(svg_path)
		for size in SIZES:
			folder = os.path.join(TOOLS, 'images', f'{size}x{size}')
			os.makedirs(folder, exist_ok=True)
			out = os.path.join(folder, f'{name}.png')
			cairosvg.svg2png(url=svg_path, write_to=out, output_width=size, output_height=size)
			print('wrote', out)


def main() -> int:
	os.chdir(TOOLS)
	render_preview_pngs()
	for size in SIZES:
		make_notepad4_toolbar_bitmap(size)
		src = f'Toolbar{size}.bmp'
		dst = os.path.join(ROOT, 'res', f'Toolbar{size}.bmp')
		with open(src, 'rb') as fd:
			data = fd.read()
		with open(dst, 'wb') as fd:
			fd.write(data)
		print('updated', dst)
	return 0


if __name__ == '__main__':
	sys.exit(main())
