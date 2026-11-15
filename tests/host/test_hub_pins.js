// The hub's pins: esp32-input/hub_pins.h (the code) and docs/HUB_PINS.md (the wiring table) must agree, no GPIO may be
// used twice, and no hub source may use a pin number that hub_pins.h does not define.
const fs = require('fs'), path = require('path');
const HUB = path.join(__dirname, '..', '..', 'esp32-input');
const H = fs.readFileSync(path.join(HUB, 'hub_pins.h'), 'utf8');
const DOC = fs.readFileSync(path.join(__dirname, '..', '..', 'docs', 'HUB_PINS.md'), 'utf8');
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };
const code = {};   // define -> { gpio, term }
for (const m of H.matchAll(/^#define (\w+) +(?:GPIO_NUM_)?(\d+)\s*\/\/\s*([LR]\d+)/gm)) code[m[1]] = { gpio: +m[2], term: m[3] };
const doc = {};
const map = DOC.split('## GPIO map')[1].split('\n## ')[0];
for (const m of map.matchAll(/^\| ([LR]\d+) \| (\d+) \| (\w+) \|/gm)) doc[m[3]] = { gpio: +m[2], term: m[1] };
console.log("1. hub_pins.h and docs/HUB_PINS.md");
check(Object.keys(code).length >= 24, "hub_pins.h defines every pin with its terminal (" + Object.keys(code).length + ")");
for (const n of Object.keys(code)) check(doc[n] && doc[n].gpio === code[n].gpio && doc[n].term === code[n].term,
  n + ": GPIO " + code[n].gpio + " at " + code[n].term + " in the code; doc says " + (doc[n] ? "GPIO " + doc[n].gpio + " at " + doc[n].term : "nothing"));
for (const n of Object.keys(doc)) check(code[n], n + " is in the doc table but not in hub_pins.h");
console.log("2. each GPIO and terminal once");
const g = Object.values(code).map(v => v.gpio), t = Object.values(code).map(v => v.term);
check(new Set(g).size === g.length, "no GPIO used twice: " + g.sort((a, b) => a - b).join(","));
check(new Set(t).size === t.length, "no terminal used twice");
check(g.every(x => !(x >= 6 && x <= 11) && x !== 1 && x !== 3), "nothing on the flash pins (6-11) or the USB serial pins (1, 3)");
console.log("3. no pin numbers outside hub_pins.h");
for (const f of fs.readdirSync(HUB).filter(f => /\.(ino|h)$/.test(f) && f !== 'hub_pins.h')) {
  const src = fs.readFileSync(path.join(HUB, f), 'utf8').replace(/\/\/[^\n]*/g, '');
  const bad = [...src.matchAll(/\b(pinMode|digitalRead|digitalWrite|analogRead|analogReadMilliVolts|attachInterrupt|digitalPinToInterrupt)\s*\(\s*(\d+)/g)].map(m => m[0]);
  const defs = [...src.matchAll(/^#define \w*(PIN|SDA|SCL|SCK|MOSI|MISO|_CS|GDO\d)\w* +(?:GPIO_NUM_)?\d+\b/gm)].map(m => m[0]);
  check(bad.length === 0 && defs.length === 0, f + ": pins only by their hub_pins.h names" + (bad.length || defs.length ? " - found " + bad.concat(defs).join("; ") : ""));
}
console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
