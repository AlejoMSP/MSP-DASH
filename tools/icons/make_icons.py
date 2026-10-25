#!/usr/bin/env python3
"""Builds the gauge icon fonts from Material Design Icons (Apache-2.0, pictogrammers.com).

  npm pack @mdi/font @mdi/js   (in a scratch folder, then untar both)  ->  MDI_FONT=<.../mdi-font-x> MDI_JS=<.../mdi-js-x>
  npm install lv_font_conv@1.5.2                                    ->  LV_FONT_CONV=<.../node_modules/.bin/lv_font_conv>
  python3 tools/icons/make_icons.py

Reads tools/icons/icons.txt. Icon n (its line, from 1) becomes character U+E000+n in font_icons_<size>.c, one per caption
size (14, 20, 28, 36, 48), and gauge_icons.h gets the table the firmware and the gauge editor use (the editor's previews are
the same icons as SVG paths from @mdi/js, so screen and editor always match).
"""
import os, re, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
SKETCH = os.path.join(HERE, '..', '..', 'esp32s3-amoled-bsp-base')
SIZES = [14, 20, 28, 36, 48]
font_dir = os.environ['MDI_FONT']; js_dir = os.environ['MDI_JS']; conv = os.environ.get('LV_FONT_CONV', 'lv_font_conv')
scss = open(os.path.join(font_dir, 'scss', '_variables.scss')).read()
js = open(os.path.join(js_dir, 'mdi.js')).read()
version = re.search(r'"version":\s*"([^"]+)"', open(os.path.join(font_dir, 'package.json')).read()).group(1)
ttf = os.path.join(font_dir, 'fonts', 'materialdesignicons-webfont.ttf')

icons = []
for line in open(os.path.join(HERE, 'icons.txt')):
    line = line.strip()
    if not line or line.startswith('#'): continue
    name, label = [x.strip() for x in line.split('|', 1)]
    m = re.search(r'"%s":\s*([0-9A-Fa-f]+)' % re.escape(name), scss)
    if not m: sys.exit('MDI has no icon named "%s"' % name)
    camel = 'mdi' + ''.join(p[:1].upper() + p[1:] for p in name.split('-'))
    pm = re.search(r'export var %s = "([^"]+)"' % camel, js)
    if not pm: sys.exit('@mdi/js has no %s' % camel)
    icons.append((name, label, int(m.group(1), 16), pm.group(1)))

ranges = ','.join('0x%X=>0x%X' % (cp, 0xE000 + i + 1) for i, (_, _, cp, _) in enumerate(icons))
for size in SIZES:
    out = os.path.join(SKETCH, 'font_icons_%d.c' % size)
    subprocess.check_call([conv, '--font', ttf, '-r', ranges, '--size', str(size), '--bpp', '4', '--no-compress', '--format', 'lvgl',
                           '--lv-include', 'lvgl.h', '-o', out])   # the font is named after the file: font_icons_<size>

def utf8(cp): return ''.join('\\x%02X' % b for b in chr(cp).encode('utf-8'))
h = ['// gauge_icons.h - MADE BY tools/icons/make_icons.py from Material Design Icons %s (Apache-2.0). Do not edit by hand.' % version,
     '// Icon n (1..GAUGE_ICON_COUNT) is character U+E000+n in font_icons_<14|20|28|36|48>; 0 = no icon.',
     '#pragma once', '#include "lvgl.h"', '#ifdef __cplusplus', 'extern "C" {', '#endif']
h += ['extern const lv_font_t font_icons_%d;' % s for s in SIZES]
h += ['#ifdef __cplusplus', '}', '#endif',
      '#include "gauge_icon_count.h"',
      '// UTF-8 text of each icon (index 0 unused)',
      'static const char *const GAUGE_ICON_TEXT[GAUGE_ICON_COUNT + 1] = { "", %s };' % ', '.join('"%s"' % utf8(0xE000 + i + 1) for i in range(len(icons))),
      'static const char *const GAUGE_ICON_NAME[GAUGE_ICON_COUNT + 1] = { "", %s };' % ', '.join('"%s"' % n for n, _, _, _ in icons),
      '// The icon font for a caption size (14/20/28/36/48; anything else: the nearest).',
      'static inline const lv_font_t *gauge_icon_font(int32_t size) {',
      '  if (size <= 17) return &font_icons_14; if (size <= 24) return &font_icons_20; if (size <= 32) return &font_icons_28;',
      '  if (size <= 42) return &font_icons_36; return &font_icons_48;',
      '}',
      ]
js_h = ['// MADE BY tools/icons/make_icons.py. For the gauge editor (no LVGL needed): [label, SVG path (24x24 viewBox), MDI name] per icon,',
        '// index 0 = none. The same icons as the screen draws (gauge_icons.h).', '#pragma once',
        '#define GAUGE_ICONS_JS "const ICONS = [[\\"None\\", \\"\\", \\"\\"], %s];\\n"' % ', '.join('[\\"%s\\", \\"%s\\", \\"%s\\"]' % (l, p, n) for n, l, _, p in icons)]
open(os.path.join(SKETCH, 'gauge_icons_js.h'), 'w').write('\n'.join(js_h) + '\n')
open(os.path.join(SKETCH, 'gauge_icons.h'), 'w').write('\n'.join(h) + '\n')
open(os.path.join(SKETCH, 'gauge_icon_count.h'), 'w').write('// MADE BY tools/icons/make_icons.py. How many gauge icons this firmware has (gauge_icons.h); no LVGL needed.\n#pragma once\n#define GAUGE_ICON_COUNT %d\n' % len(icons))
print('%d icon(s), %d sizes, MDI %s' % (len(icons), len(SIZES), version))
