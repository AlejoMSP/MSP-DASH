// Drives the hub's /custom page (esp32-input/hub_pages.h) with a fake hub: the list, the editor, the live preview
// (its JS decoder must match custom_can_decode() in custom_slots.h) and what Save sends.
const fs = require('fs'), path = require('path'), vm = require('vm');
const PAGES = fs.readFileSync(path.join(__dirname, '..', '..', 'esp32-input', 'hub_pages.h'), 'utf8');
const script = PAGES.match(/CUSTOM_HTML\[\] PROGMEM = R"HTML\(([\s\S]*?)\)HTML"/)[1].match(/<script>([\s\S]*?)<\/script>/)[1];
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };
const settle = async () => { for (let i = 0; i < 20; i++) await new Promise(r => setImmediate(r)); };
const empty = i => ({ i, name: "", unit: "", dec: 0, min: 0, max: 0, src: 0, id: 0, start: 0, bits: 0, bit: 0, be: 0, sgn: 0, scale: 0, offset: 0, has: false, value: 0 });
let slots = Array.from({ length: 16 }, (_, i) => empty(i));
slots[0] = { i: 0, name: "Wastegate <Duty>", unit: "%", dec: 1, min: 0, max: 100, src: 1, id: 0x208, start: 0, bits: 16, bit: 0, be: 0, sgn: 0, scale: 0.1, offset: 0, has: true, value: 42.5 };
const can = { total: 9, ids: [{ id: 0x208, n: 9, age: 20, d: "10 27 FF FE 80 01 02 03 " }, { id: 0x5F2, n: 3, age: 30, d: "00 64 " }] };
const els = {}; const el = id => els[id] || (els[id] = { id, innerHTML: "", textContent: "", value: "", style: {}, className: "" });
const gets = [];
function run(search, setResult) {
  for (const k of Object.keys(els)) delete els[k];
  const ctx = { document: { getElementById: el }, location: { search }, URLSearchParams, setInterval: () => 0, console,
    fetch: async url => { gets.push(url);
      if (url === "/custom.json") return { json: async () => JSON.parse(JSON.stringify(slots)) };
      if (url === "/can.json") return { json: async () => can };
      if (url.startsWith("/custom/set") || url.startsWith("/custom/clear")) { const r = setResult || { ok: true, t: "Saved." }; return { ok: r.ok, text: async () => r.t }; }
      throw new Error(url); } };
  vm.createContext(ctx); vm.runInContext(script, ctx); return ctx;
}

(async () => {
  let ctx = run(""); await settle();
  console.log("1. the list");
  const list = el("list").innerHTML;
  check(/Wastegate &lt;Duty&gt;/.test(list) && /42\.5 %/.test(list) && /0x208 byte 0, 16 bit/.test(list), "slot 1 shows its name (escaped), value and source");
  check((list.match(/Set up/g) || []).length === 15, "the 15 empty slots offer Set up");
  check(/0x208/.test(el("ids").innerHTML) && /0x5F2/.test(el("ids").innerHTML), "the CAN ID box suggests the IDs on the bus");

  console.log("2. the decoder matches the firmware's (same cases as test_custom_slots.cpp)");
  const b = [0x10, 0x27, 0xFF, 0xFE, 0x80, 0x01, 0x02, 0x03];
  const D = (start, bits, be, sgn, scale, offset, bit) => JSON.stringify(vm.runInContext(`decode(${JSON.stringify({ start, bits, be, sgn, scale, offset, bit: bit || 0 })}, ${JSON.stringify(b)})`, ctx));
  check(D(0, 16, 0, 0, 0.1, 0) === "1000", "16 bit little endian x 0.1 = 1000");
  check(D(0, 16, 1, 0, 1, 0) === String(0x1027), "16 bit big endian");
  check(D(2, 16, 1, 1, 1, 0) === "-2", "signed 16 bit big endian = -2");
  check(D(4, 8, 0, 1, 1, 0) === "-128", "signed 8 bit = -128");
  check(D(4, 8, 0, 0, 1, -40) === "88", "offset -40");
  check(D(4, 32, 0, 0, 1, 0) === String(0x03020180), "32 bit little endian");
  check(D(4, 32, 1, 1, 1, 0) === String(0x80010203 | 0), "signed 32 bit big endian (negative)");
  check(D(5, 1, 0, 0, 1, 0, 0) === "1" && D(5, 1, 0, 0, 1, 0, 1) === "0", "one bit");
  check(D(6, 32, 0, 0, 1, 0) === "null", "a field past the end of the frame: null");

  console.log("3. setting up a slot");
  vm.runInContext('edit(1)', ctx);
  check(el("ed").style.display === "" && el("edtitle").textContent === "Slot 2" && el("min").value === 0 && el("max").value === 100 && el("scale").value === 1, "the editor opens with sensible defaults");
  Object.assign(el("name"), { value: " Ethanol Temp " }); el("unit").value = "F"; el("dec").value = "0"; el("min").value = "0"; el("max").value = "250";
  el("cid").value = "0x208"; el("start").value = "4"; el("bits").value = "8"; el("be").value = "0"; el("sgn").value = "0"; el("scale").value = "1"; el("offset").value = "-40";
  vm.runInContext('shape();pv()', ctx);
  check(el("preview").textContent === "88 F" && /0x208: 10 27 FF FE 80 01 02 03/.test(el("raw").textContent), "live preview from the frame on the bus: " + el("preview").textContent);
  el("bits").value = "1"; el("start").value = "5"; el("bit").value = "0"; vm.runInContext('shape();pv()', ctx);
  check(el("preview").textContent === "ON" && el("onebit").style.display === "" && el("lin").style.display === "none", "1 bit: ON/off, bit box shown, scale hidden");
  el("bits").value = "32"; el("start").value = "6"; vm.runInContext('shape();pv()', ctx);
  check(el("preview").textContent === "does not fit in this frame", "a field that runs off the end says so");
  el("cid").value = "0x123"; vm.runInContext('pv()', ctx);
  check(/not heard on the bus/.test(el("raw").textContent), "an ID not on the bus says so");
  el("cid").value = "0x208"; el("bits").value = "8"; el("start").value = "4"; vm.runInContext('shape()', ctx);
  gets.length = 0; await vm.runInContext('save()', ctx); await settle();
  const set = gets.find(u => u.startsWith("/custom/set"));
  const q = new URLSearchParams(set.split("?")[1]);
  check(q.get("i") === "1" && q.get("name") === "Ethanol Temp" && q.get("id") === "0x208" && q.get("start") === "4" && q.get("bits") === "8" && q.get("offset") === "-40" && q.get("unit") === "F",
    "Save sends the slot, trimmed name, ID in hex and the field: " + set);
  check(el("msg").textContent === "Saved." && el("msg").className === "", "says Saved");
  el("name").value = ""; await vm.runInContext('save()', ctx);
  check(el("msg").textContent === "Give the sensor a name" && el("msg").className === "bad", "no name: not sent");
  ctx = run("", { ok: false, t: "That CAN field does not fit" }); await settle();
  vm.runInContext('edit(1)', ctx); el("name").value = "X"; el("cid").value = "0x208"; await vm.runInContext('save()', ctx); await settle();
  check(el("msg").textContent === "That CAN field does not fit" && el("msg").className === "bad", "the hub's refusal is shown in red");

  console.log("4. 'Make a sensor' from the Sensors page");
  ctx = run("?id=0x5F2"); await settle(); await settle();
  check(el("edtitle").textContent === "Slot 2" && el("cid").value === "0x5F2", "opens the first empty slot with that CAN ID filled in");

  console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})();
