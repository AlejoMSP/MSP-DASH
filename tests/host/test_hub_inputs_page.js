// The hub's Inputs and pins page (hub_pages.h INPUTS_HTML) with a fake hub: the pin table (who uses each pin, moving a
// function), the inputs list, the editor for a button, a switch and an analog sensor.
const vm = require('vm'), { hubScript } = require('./hub_page.js');
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };
const settle = async () => { for (let i = 0; i < 20; i++) await new Promise(r => setImmediate(r)); };
const gp = (g, caps, user, note) => ({ g, caps, user: user || "", note: note || "", low: false });
const noInput = { type: 0, pin: 0, contact: 0, slot: 0, ap: 0, pp: 0, ah: 0, ph: 0, preset: 0, mult: 1, v1: 0, val1: 0, v2: 3.3, val2: 100, pullup: 2490, name: "", active: false, value: 0 };
const D = {
  roles: [{ key: "btn_next", name: "Right (next) button", gpio: 25, def: 25, need: 9, mcp: true }, { key: "fuel", name: "Fuel level sender", gpio: 36, def: 36, need: 4, mcp: false },
          { key: "can_tx", name: "CAN TX", gpio: 21, def: 21, need: 2, mcp: false }],
  outputs: [{ type: 1, pin: 13, low: 1, blink: 0, sig: 2, cond: 1, input: 0, level: 200, hyst: 5, name: "Fan relay", on: true },
            { type: 3, pin: 108, low: 0, blink: 1, sig: 0, cond: 0, input: 0, level: 0, hyst: 0, name: "Light bar", on: false }].concat(Array(14).fill({ type: 0, pin: 0, low: 0, blink: 0, sig: 0, cond: 0, input: 0, level: 0, hyst: 0, name: "", on: false })),
  gpios: [gp(0, 11, "", "BOOT button"), gp(2, 11, "Input 1 (Horn)"), gp(12, 11), gp(21, 11, "CAN TX"), gp(25, 11, "Right (next) button"), gp(33, 15, "Input 2 (Oil psi)"), gp(34, 5, "", "input only"), gp(36, 5, "Fuel level sender")],
  mcp: [0x27, 0], mcpbits: [0xFFFF, 0xFFFF], ads: [0x48, 0, 0, 0], adsv: [1.234, 0.5, 0, 3.1].concat(Array(12).fill(0)), adsusers: ["", "Input 9 (Boost)", "", ""].concat(Array(12).fill("")), mcpusers: Array(32).fill("").map((u, n) => n === 3 ? "OLED lamp 5" : ""),
  inputs: [Object.assign({}, noInput, { type: 1, pin: 2, contact: 0, ap: 1, ah: 8, ph: 2, name: "Horn", active: true }),
           Object.assign({}, noInput, { type: 3, pin: 33, slot: 4, preset: 1, name: "Oil psi", active: true, value: 42.4 })].concat(Array(14).fill(noInput)),
  slots: Array(16).fill(0).map((x, k) => ({ name: k === 4 ? "Oil psi" : k === 0 ? "Wastegate" : "", src: k === 4 ? 2 : k === 0 ? 1 : 0 })),
  presets: [{ name: "Custom (two-point)", unit: "" }, { name: "Pressure 0-100 psi (0.5-4.5 V)", unit: "psi" }, { name: "Pressure 0-150 psi", unit: "psi" }, { name: "P200", unit: "psi" },
            { name: "Voltage 0-16 V", unit: "V" }, { name: "Voltage 0-5 V", unit: "V" }, { name: "Temp sensor Bosch NTC (2.5k)", unit: "F" }, { name: "GM", unit: "F" }],
  actions: ["Nothing", "Next page", "Previous page", "Next gauge", "Go to page", "Dim all", "Brighten all", "Color scheme", "Race mode", "Clear warning", "Log marker", "Show IP addresses", "OLED next page"] };
const els = {}; const el = id => els[id] || (els[id] = { id, innerHTML: "", textContent: "", value: "" });
const gets = [];
const ctx = { document: { getElementById: el }, URLSearchParams, console, setInterval: () => 0, setTimeout: () => 0, confirm: () => true,
  fetch: async u => { gets.push(u); return { ok: true, json: async () => D, text: async () => "Saved" }; } };
vm.createContext(ctx); vm.runInContext(hubScript('INPUTS_HTML'), ctx);
const R = c => vm.runInContext(c, ctx);
(async () => {
  await settle();
  console.log("1. the pin table");
  const p = el("pins").innerHTML;
  check(/<td>GPIO 25<\/td>[\s\S]*?Right \(next\) button/.test(p) && /<td>GPIO 12<\/td>[\s\S]*?class="free">free/.test(p), "each GPIO with what uses it, or free");
  check(/GPIO 2<\/td>[\s\S]*?Input 1 \(Horn\)/.test(p) && /GPIO 33<\/td>[\s\S]*?Input 2 \(Oil psi\)/.test(p) && /MCP23017 #1 PA3<\/td>[\s\S]*?OLED lamp 5/.test(p) && /MCP23017 #1 PB7/.test(p) && !/MCP23017 #2/.test(p), "inputs, lamps and the expander's pins (board 2 not fitted: not listed)");
  const fuelRow = p.split("<tr>").find(r => r.includes("GPIO 36"));
  check(/onchange="if\(this.value!==''\)movePin\('fuel',\+this.value\)"/.test(fuelRow) && /value="34">GPIO 34/.test(fuelRow) && !/value="12"/.test(fuelRow) && !/value="33"/.test(fuelRow), "fuel can move to a free analog GPIO (34), not a digital one (12) or a taken one (33)");
  await R("movePin('fuel', 34)"); await settle();
  check(gets.includes("/pins/set?role=fuel&gpio=34"), "moving sends the role and the GPIO (the hub restarts)");
  check(/GPIO 12<\/td>[\s\S]*?onclick="assignPin\(12,11\)">Set up an input/.test(p) && /MCP23017 #1 PB7<\/td>[\s\S]*?onclick="assignPin\(115,11\)">Set up an input/.test(p), "free pins (GPIO and expander) offer 'Set up an input'");
  check(/GPIO 2<\/td>[\s\S]*?onclick="edit\(0\)">Edit input/.test(p) && /PA3<\/td>[\s\S]*?href="\/oled"/.test(p), "a pin with an input offers Edit; a lamp's pin links to the OLED page");
  R("assignPin(115, 9)"); let ae = el("editor").innerHTML;
  check(/<option value="115" selected>MCP23017 #1 PB7<\/option>/.test(ae) && /<option value="2" selected>Switch<\/option>/.test(ae), "from the pin table: the editor opens on that pin, as a switch");
  R("assignPin(34, 5)"); ae = el("editor").innerHTML;
  check(/<option value="34" selected>GPIO 34<\/option>/.test(ae) && /<option value="3" selected>Analog sensor<\/option>/.test(ae), "an analog-only pin opens as a sensor");
  R("editing = null; drawEditor()"); check(el("editor").innerHTML === "", "Cancel closes the editor");
  console.log("1b. buttons onto the expander, outputs");
  const btnRow = p.split("<tr>").find(r => r.includes("GPIO 25"));
  check(/value="100">MCP23017 #1 PA0/.test(btnRow) && !/value="103"/.test(btnRow), "a page button can move onto a free expander pin (not the lamp's PA3)");
  check(!/value="10\d">MCP/.test(fuelRow), "the fuel sender cannot (analog)");
  check(/GPIO 12<\/td>[\s\S]*?assignOut\(12\)">Set up an output/.test(p) && !/GPIO 34<\/td>[^]*?assignOut\(34\)/.test(p.split("<tr>").find(r => r.includes("GPIO 34"))), "free GPIOs that can drive offer 'Set up an output' (not input-only 34)");
  const o = el("outputs").innerHTML;
  check(/Fan relay<\/td><td>Coolant Temp above 200<\/td><td>GPIO 13 \(LOW = on\)<\/td><td><span class="on">ON/.test(o), "output list: signal, level, pin, LOW = on, state");
  check(/Light bar<\/td><td>toggled, blinking<\/td><td>MCP23017 #1 PB0<\/td><td>off<\/td><td><button onclick="toggleOut\(1\)">Turn on/.test(o), "a toggled output on the expander, with a Turn on button");
  R("assignOut(12)"); let oe = el("oeditor").innerHTML;
  check(/Output 3/.test(oe) && /<option value="12" selected>GPIO 12/.test(oe) && !/>GPIO 34</.test(oe) && /MCP23017 #1 PA0/.test(oe), "output editor on GPIO 12; output pins only (no 34), expander pins offered");
  check(/LOW \(most relay modules\)/.test(oe) && /<option value="1" selected>above<\/option>/.test(oe) && /Hysteresis/.test(oe), "relay level, condition, hysteresis");
  el("o_name").value = "Pump"; el("o_pin").value = "12"; el("o_low").value = "1"; el("o_sig").value = "6"; el("o_cond").value = "1"; el("o_level").value = "230"; el("o_hyst").value = "10"; el("o_blink").checked = false;
  await R("saveOut()"); await settle();
  let oq = new URLSearchParams(gets.filter(u => u.startsWith("/outputs/set?")).pop().split("?")[1]);
  check(oq.get("i") === "2" && oq.get("type") === "1" && oq.get("pin") === "12" && oq.get("low") === "1" && oq.get("sig") === "6" && oq.get("level") === "230" && oq.get("hyst") === "10", "output saved: GPIO 12, LOW = on, Oil Temp above 230, 10 back");
  await R("toggleOut(1)"); await settle(); check(gets.includes("/outputs/toggle?i=1"), "Turn on / off from the list");
  console.log("1c. ADS1115 channels");
  check(/ADS1115 #1 A0<\/td>[\s\S]*?now 1.234 V[\s\S]*?class="free">free[\s\S]*?assignPin\(140,4\)">Set up an input/.test(p) && !/assignOut\(140\)/.test(p), "an ADS channel: its volts, free, can be an input (not an output)");
  check(/ADS1115 #1 A1<\/td>[\s\S]*?Input 9 \(Boost\)/.test(p) && /ADS1115 #1 A3/.test(p) && !/ADS1115 #2/.test(p), "used channels named; only found boards listed");
  check(/value="140">ADS1115 #1 A0/.test(fuelRow) && !/value="141"/.test(fuelRow) && !/value="140"/.test(btnRow), "the fuel sender can move onto a free ADS channel; a button cannot");
  R("assignPin(140, 4)"); { const ae = el("editor").innerHTML;
    check(/<option value="3" selected>Analog sensor<\/option>/.test(ae) && /<option value="140" selected>ADS1115 #1 A0<\/option>/.test(ae) && !/value="141"/.test(ae), "set up on an ADS channel: an analog sensor there (taken A1 not offered)"); }
  R("editing = null; drawEditor()");
  console.log("2. the inputs list");
  const l = el("inputs").innerHTML;
  check(/Horn<\/td><td>Button \(active LOW\)<\/td><td>GPIO 2<\/td><td><span class="on">ACTIVE<\/span>/.test(l), "button: active LOW, GPIO 2, active now");
  check(/Oil psi<\/td><td>Pressure 0-100 psi \(0.5-4.5 V\), sensor 5<\/td><td>GPIO 33<\/td><td>42.40 psi/.test(l), "sensor: preset, slot, live reading");
  console.log("3. editing");
  R("edit(-1)"); let e = el("editor").innerHTML;
  check(/Input 3/.test(e) && /<option value="1" selected>Button<\/option>/.test(e), "Add: the first free input, as a button");
  check(/<option value="12">GPIO 12<\/option>/.test(e) && !/>GPIO 25</.test(e) && !/>GPIO 2</.test(e) && /MCP23017 #1 PA0/.test(e) && !/MCP23017 #1 PA3/.test(e), "pin list: only free pins (not 25, not 2, not the lamp's PA3)");
  check(/Active when LOW \(switch to ground, optocoupler\)/.test(e) && /Active when HIGH \(12 V through a divider\)/.test(e) && /On a press/.test(e) && /On a 1 s hold/.test(e), "active LOW / HIGH, press and hold actions");
  el("e_name").value = "Lap button"; el("e_pin").value = "12"; el("e_contact").value = "1"; el("e_ap").value = "10"; el("e_pp").value = "0"; el("e_ah").value = "4"; el("e_ph").value = "3";
  await R("saveIn()"); await settle();
  let q = new URLSearchParams(gets.filter(u => u.startsWith("/inputs/set?")).pop().split("?")[1]);
  check(q.get("i") === "2" && q.get("type") === "1" && q.get("pin") === "12" && q.get("contact") === "1" && q.get("ap") === "10" && q.get("ah") === "4" && q.get("ph") === "3" && q.get("name") === "Lap button",
    "button saved: GPIO 12, active HIGH, press = log marker, hold = go to page 3");
  R("edit(-1); editing.type = 3; drawEditor()"); e = el("editor").innerHTML;
  check(/<option value="34"( selected)?>GPIO 34<\/option>/.test(e) && !/>GPIO 33</.test(e) && !/GPIO 12</.test(e) && !/MCP23017/.test(e), "analog: only free analog GPIOs (34; 33 is taken), no expander pins");
  check(/<option value="1"( selected)?>Sensor 2 \(free\)/.test(e) && !/Sensor 1 \(Wastegate\)/.test(e) && !/Sensor 5/.test(e), "slot list: free slots only (not the CAN one, not one used by another input)");
  R("editing.preset = 0; drawEditor()"); e = el("editor").innerHTML;
  check(/Divider multiplier/.test(e) && /Sensor volts, point 1/.test(e) && /Sensor volts, point 2/.test(e), "custom sensor: multiplier and two points");
  R("editing.preset = 6; drawEditor()"); check(/Pull-up resistor/.test(el("editor").innerHTML), "temperature sensor: its pull-up");
  el("e_name").value = "Coolant 2"; el("e_pin").value = "34"; el("e_slot").value = "1"; el("e_pullup").value = "2200";
  await R("saveIn()"); await settle();
  q = new URLSearchParams(gets.filter(u => u.startsWith("/inputs/set?")).pop().split("?")[1]);
  check(q.get("type") === "3" && q.get("pin") === "34" && q.get("preset") === "6" && q.get("slot") === "1" && q.get("pullup") === "2200", "sensor saved: GPIO 34, Bosch NTC, slot 2, 2.2k pull-up");
  await R("clearIn(0)"); await settle();
  check(gets.includes("/inputs/clear?i=0"), "Remove");
  console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})();
