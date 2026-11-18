// The hub's pins: the roles in esp32-input/hub_pins.h (with their default GPIO and terminal) and docs/HUB_PINS.md must
// agree, no default GPIO or terminal may be used twice, each default must be able to do its job, and no hub source may
// use a pin number that is not from hub_pins.h.
const fs = require('fs'), path = require('path');
const HUB = path.join(__dirname, '..', '..', 'esp32-input');
const H = fs.readFileSync(path.join(HUB, 'hub_pins.h'), 'utf8');
const DOC = fs.readFileSync(path.join(__dirname, '..', '..', 'docs', 'HUB_PINS.md'), 'utf8');
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };
const CAP = { CAP_IN: 1, CAP_OUT: 2, CAP_ADC: 4, CAP_PULLUP: 8 };
const code = {};
for (const m of H.matchAll(/\{ "(\w+)",\s*"([^"]+)",\s*(\d+),\s*([A-Z_ |]+),\s*"([LR]\d+)"\s*\}/g))
  code[m[1]] = { gpio: +m[3], term: m[5], need: m[4].split('|').map(x => CAP[x.trim()]).reduce((a, b) => a | b, 0) };
const enumN = (H.match(/enum HubPinRole \{([\s\S]*?)PR_COUNT/) || ["", ""])[1].split(',').filter(x => x.trim()).length;
const doc = {};
const map = DOC.split('## GPIO map')[1].split('\n## ')[0];
for (const m of map.matchAll(/^\| ([LR]\d+) \| (\d+) \| (\w+) \|/gm)) doc[m[3]] = { gpio: +m[2], term: m[1] };
// gpio_caps() from the header, for "can this default do its job"
const CAPS = {};   // gpio_caps() from the header: cases collect until their return
{ const sw = H.match(/static inline uint8_t gpio_caps\(int g\) \{([\s\S]*?)\n\}/)[1]; let pend = [];
  for (const line of sw.split('\n')) { pend.push(...[...line.matchAll(/case (\d+):/g)].map(m => +m[1])); const r = line.match(/return ([A-Z_ |]+);/);
    if (r && pend.length) { const v = r[1].split('|').map(x => CAP[x.trim()]).reduce((a, b) => a | b, 0); pend.forEach(x => CAPS[x] = v); pend = []; } } }
const caps = g => CAPS[g] || 0;
console.log("1. hub_pins.h and docs/HUB_PINS.md");
check(Object.keys(code).length === 19 && enumN === 19, "19 roles, one per enum entry (" + Object.keys(code).length + " / " + enumN + ")");
for (const n of Object.keys(code)) check(doc[n] && doc[n].gpio === code[n].gpio && doc[n].term === code[n].term,
  n + ": GPIO " + code[n].gpio + " at " + code[n].term + " in the code; doc says " + (doc[n] ? "GPIO " + doc[n].gpio + " at " + doc[n].term : "nothing"));
for (const n of Object.keys(doc)) check(code[n], n + " is in the doc table but not in hub_pins.h");
{ const nx = +(H.match(/#define PIN_BTN_NEXT_DEF (\d+)/) || [])[1], pv = +(H.match(/#define PIN_BTN_PREV_DEF (\d+)/) || [])[1];
  const btn = DOC.split('## Default buttons')[1] || "";
  check(new RegExp("\\| L14 \\| " + nx + " \\| Right").test(btn) && new RegExp("\\| L15 \\| " + pv + " \\| Left").test(btn), "default page buttons in the doc: right GPIO " + nx + ", left GPIO " + pv);
  check(!Object.values(code).some(v => v.gpio === nx || v.gpio === pv), "no fixed function on the default button pins"); }
console.log("2. defaults: each GPIO and terminal once, and able to do the job");
const g = Object.values(code).map(v => v.gpio), t = Object.values(code).map(v => v.term);
check(new Set(g).size === g.length, "no default GPIO used twice: " + g.slice().sort((a, b) => a - b).join(","));
check(new Set(t).size === t.length, "no terminal used twice");
for (const n of Object.keys(code)) check((caps(code[n].gpio) & code[n].need) === code[n].need, n + ": GPIO " + code[n].gpio + " can do it");
check(g.every(x => !(x >= 6 && x <= 11) && x !== 1 && x !== 3), "nothing on the flash pins (6-11) or the USB serial pins (1, 3)");
console.log("3. no pin numbers outside hub_pins.h");
for (const f of fs.readdirSync(HUB).filter(f => /\.(ino|h)$/.test(f) && f !== 'hub_pins.h')) {
  const src = fs.readFileSync(path.join(HUB, f), 'utf8').replace(/\/\/[^\n]*/g, '');
  const bad = [...src.matchAll(/\b(pinMode|digitalRead|digitalWrite|analogRead|analogReadMilliVolts|attachInterrupt|digitalPinToInterrupt)\s*\(\s*(\d+)/g)].map(m => m[0]);
  const defs = [...src.matchAll(/^#define \w*(PIN|SDA|SCL|SCK|MOSI|MISO|_CS|GDO\d)\w* +(?:GPIO_NUM_)?\d+\b/gm)].map(m => m[0]);
  check(bad.length === 0 && defs.length === 0, f + ": pins only through hub_pins.h" + (bad.length || defs.length ? " - found " + bad.concat(defs).join("; ") : ""));
}
console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
