#!/usr/bin/env python3
"""Makes esp32-input/oled_icons.h: the gauge icons as 32x32 1-bit bitmaps for the hub's SSD1306 OLED, from the same
icon font the screens use (run make_icons.py first). Needs gcc and LVGL's source.
    LVGL=/home/claude/lvgl LV_CONF_DIR=/home/claude python3 tools/icons/make_oled_icons.py
"""
import os, re, subprocess
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.join(HERE, '..', '..')
SK = os.path.join(ROOT, 'esp32s3-amoled-bsp-base'); L = os.environ['LVGL']; C = os.environ['LV_CONF_DIR']
src = [os.path.join(HERE, 'oled_dump.c'), os.path.join(SK, 'font_icons_36.c')] + [os.path.join(L, 'src', p) for p in
       ['font/lv_font.c', 'font/lv_font_fmt_txt.c', 'misc/lv_utils.c', 'misc/lv_mem.c', 'misc/lv_log.c', 'misc/lv_tlsf.c', 'misc/lv_gc.c']]
subprocess.check_call(['gcc', '-O1', '-DLV_CONF_INCLUDE_SIMPLE', '-I' + C, '-I' + L, '-I' + SK] + src + ['-o', '/tmp/oled_dump'])
out = subprocess.check_output(['/tmp/oled_dump']).decode()
icons = {}
for blk in out.split('ICON ')[1:]:
    lines = blk.strip().split('\n'); n = int(lines[0]); rows = lines[1:33]
    data = []
    for page in range(4):                       # SSD1306 layout: 8-pixel vertical bytes, page by page
        for x in range(32):
            b = 0
            for bit in range(8):
                if rows[page * 8 + bit][x] == '#': b |= 1 << bit
            data.append(b)
    icons[n] = data
js = re.search(r'#define GAUGE_ICONS_JS (".*")', open(os.path.join(SK, 'gauge_icons_js.h')).read()).group(1)
h = ['// oled_icons.h - MADE BY tools/icons/make_oled_icons.py from the screens\' icon font (Material Design Icons).',
     '// Icon n (1..OLED_ICON_COUNT, the same numbers as gauge_icons.h): 32x32, 1 bit, SSD1306 page order (4 pages x 32 columns).',
     '#pragma once', '#include <stdint.h>', '#define OLED_ICON_COUNT %d' % len(icons),
     'static const uint8_t OLED_ICONS[OLED_ICON_COUNT + 1][128] = {', '  {0},']
for n in sorted(icons):
    h.append('  {' + ','.join('0x%02X' % b for b in icons[n]) + '},')
h += ['};', '// For the hub\'s /oled page: [label, SVG path, MDI name] per icon (same list as the screens\' gauge editor).',
      '#define OLED_ICONS_JS ' + js]
# 8x8 text font (unscii-8, public domain, from LVGL's font folder): ASCII 32..126, 8 column bytes each (bit 0 = top)
from PIL import Image, ImageDraw, ImageFont
fnt = ImageFont.truetype(os.path.join(L, 'scripts', 'built_in_font', 'unscii-8.ttf'), 8)
h.append('// 8x8 text (unscii-8, public domain): ASCII 32..126, 8 column bytes per character, bit 0 = top row.')
h.append('static const uint8_t OLED_FONT8[95][8] = {')
for c in range(32, 127):
    im = Image.new('1', (8, 8), 0); ImageDraw.Draw(im).text((0, 0), chr(c), font=fnt, fill=1)
    cols = [sum((1 << y) for y in range(8) if im.getpixel((x, y))) for x in range(8)]
    h.append('  {' + ','.join('0x%02X' % v for v in cols) + '},  // ' + (chr(c) if c not in (92,) else 'backslash'))
h.append('};')
open(os.path.join(ROOT, 'esp32-input', 'oled_icons.h'), 'w').write('\n'.join(h) + '\n')
print('%d OLED icons' % len(icons))
