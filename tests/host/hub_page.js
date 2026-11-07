// Pulls one hub page (esp32-input/hub_pages.h) as the compiler joins it: its raw-string pieces plus the HUB_NAV /
// HUB_NAV_STYLE macros between them. hubPage(name) -> HTML; hubScript(name) -> the page's own <script>.
const fs = require('fs'), path = require('path');
const SRC = fs.readFileSync(path.join(__dirname, '..', '..', 'esp32-input', 'hub_pages.h'), 'utf8');
const ICONS_H = fs.readFileSync(path.join(__dirname, '..', '..', 'esp32-input', 'oled_icons.h'), 'utf8');
function macro(name) {
  const m = (SRC + '\n' + ICONS_H).match(new RegExp('#define ' + name + ' "((?:[^"\\\\]|\\\\.)*)"'));
  return m ? JSON.parse('"' + m[1] + '"') : '';
}
function hubPage(name) {
  const start = SRC.indexOf('static const char ' + name + '[]');
  if (start < 0) throw new Error('no page ' + name);
  const rest = SRC.slice(start), end = rest.indexOf(')HTML";') + ')HTML";'.length, decl = rest.slice(0, end);
  let out = '', re = /R"HTML\(([\s\S]*?)\)HTML"|\b(HUB_NAV_STYLE|HUB_NAV|OLED_ICONS_JS)\b/g, m;
  while ((m = re.exec(decl))) out += m[1] !== undefined ? m[1] : macro(m[2]);
  return out;
}
function hubScript(name) { const m = hubPage(name).match(/<script>([\s\S]*?)<\/script>/); return m ? m[1] : null; }
module.exports = { hubPage, hubScript, SRC };
