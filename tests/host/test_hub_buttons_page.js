// The hub's /buttons page (esp32-input/hub_pages.h BUTTONS_HTML) with a fake hub: the button list, the editor for a
// single button and a ladder button (pins in use greyed out, Learn), the screen choice for page actions, the ECU maps.
const vm = require('vm'), { hubScript } = require('./hub_page.js');
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };
const settle = async () => { for (let i = 0; i < 20; i++) await new Promise(r => setImmediate(r)); };
const none = { kind: 0, pin: 0, high: 0, a: 0, p: 0, t: 0, mv: 0, name: "", down: false, now: 0 };
const B = (o) => Object.assign({}, none, o);
const D = {
  actions: ["Nothing", "Next page", "Previous page", "Next gauge (choose screen)", "Go to page", "Dim all", "Brighten all", "Color scheme", "Race mode",
            "Clear warning", "Log marker", "Show IP addresses", "OLED next page", "Toggle output", "ECU map (next)"],
  colors: ["Classic", "Racing orange"], targets: ["All screens", "Left", "Center", "Right", "Chosen screen"], race: false, chosen: "",
  buttons: [B({ kind: 1, pin: 26, a: 2, name: "Left" }), B({ kind: 1, pin: 25, a: 1, t: 2, name: "Right", down: true }),
            B({ kind: 2, pin: 140, a: 14, mv: 300, name: "Map", now: 3290 }), B({ kind: 2, pin: 140, a: 7, p: 255, mv: 1050, name: "Mode", now: 3290 })].concat(Array(20).fill(none)),
  pins: [[2, 11, "Input 1 (Horn)"], [12, 11, ""], [21, 11, "CAN TX"], [25, 11, "Button 2 (Right)"], [26, 11, "Button 1 (Left)"], [34, 5, ""], [36, 5, "Fuel level sender"],
         [100, 11, ""], [103, 11, "OLED lamp 5"], [140, 4, "Button ladder (Map, Mode)"], [141, 4, ""]],
  ecu: { id: 0x7F1, count: 2, cur: 1, used: true, names: ["Street", "Boost", "", "", "", "", "", ""] } };
const els = {}; const el = id => els[id] || (els[id] = { id, innerHTML: "", textContent: "", value: "", className: "" });
const gets = []; let reading = "1650", reply = { ok: true, t: "Saved" };
const ctx = { document: { getElementById: el, activeElement: null }, URLSearchParams, console, JSON, Math, setInterval: () => 1, clearInterval: () => 0, setTimeout: () => 0, confirm: () => true,
  fetch: async u => { gets.push(u);
    if (u === "/buttons.json") return { ok: true, json: async () => D };
    if (u.startsWith("/buttons/read")) return { ok: true, text: async () => reading };
    return { ok: reply.ok, text: async () => reply.t }; } };
vm.createContext(ctx); vm.runInContext(hubScript('BUTTONS_HTML'), ctx);
const R = c => vm.runInContext(c, ctx);
const lastQ = pre => new URLSearchParams(gets.filter(u => u.startsWith(pre)).pop().split("?")[1]);
(async () => {
  await settle();
  console.log("1. the list");
  const l = el("list").innerHTML;
  check(/Left<\/td><td>GPIO 26<\/td><td>-<\/td><td>Previous page on all screens/.test(l), "a single button: pin, idle, action on all screens");
  check(/Right<\/td><td>GPIO 25<\/td><td><span class="on">PRESSED<\/span><\/td><td>Next page on Center/.test(l), "pressed now; next page goes to the Center screen");
  check(/Map<\/td><td>ADS1115 #1 A0 \(ladder, 0.30 V\)<\/td><td>3.29 V<\/td><td>ECU map \(next\)/.test(l), "a ladder button: its learned volts, the pin now, the ECU map action");
  check(/Mode<\/td>[\s\S]*?Color scheme \(next each time\)<\/td>/.test(l), "non-page actions have no screen");
  check((l.match(/<tr>/g) || []).length === 5, "4 buttons (and the header)");
  console.log("2. a single button");
  R("edit(-1,1)"); let e = el("editor").innerHTML;
  check(/Button 5/.test(e) && /<option value="1" selected>Single button/.test(e), "Add a button: the first free slot, single");
  check(/<option value="12"( selected)?>GPIO 12<\/option>/.test(e) && /value="25" disabled>GPIO 25 - used by Button 2 \(Right\)/.test(e) && /value="21" disabled>GPIO 21 - used by CAN TX/.test(e)
        && /MCP23017 #1 PA0<\/option>/.test(e) && /value="103" disabled>MCP23017 #1 PA3 - used by OLED lamp 5/.test(e), "every digital pin; used ones greyed out and named");
  check(!/value="140"/.test(e) && /value="34"/.test(e), "no ADS channels for a single button; input-only 34 is offered (outside pull-up)");
  check(/Active when/.test(e) && /HIGH \(12 V through a divider\)/.test(e) && !/Learn/.test(e), "active LOW / HIGH, no Learn");
  check(/On which screen/.test(e) && /<option value="0" selected>All screens/.test(e) && /<option value="2">Center/.test(e), "next page: pick the screen");
  R("ed.a = 4; ed.p = 1; draw()"); e = el("editor").innerHTML;
  check(/Which page/.test(e) && /<option value="1" selected>Page 1/.test(e) && /On which screen/.test(e), "go to page: page box and screen");
  el("b_name").value = "Wheel up"; el("b_pin").value = "12"; el("b_high").value = "0"; el("b_p").value = "3"; el("b_t").value = "3";
  await R("saveB()"); await settle();
  let q = lastQ("/buttons/set?");
  check(q.get("i") === "4" && q.get("kind") === "1" && q.get("pin") === "12" && q.get("high") === "0" && q.get("a") === "4" && q.get("p") === "3" && q.get("t") === "3" && q.get("mv") === "0" && q.get("name") === "Wheel up",
    "saved: GPIO 12, active LOW, go to page 3 on the Right screen");
  R("edit(-1,1); ed.a = 7; ed.p = 255; draw()"); e = el("editor").innerHTML;
  check(/Color scheme/.test(e) && /Next scheme each time/.test(e) && !/On which screen/.test(e), "color scheme: the scheme box, no screen");
  R("ed.a = 13; ed.p = 1; draw()"); check(/Which output/.test(el("editor").innerHTML), "toggle output: which output");
  R("ed.a = 14; ed.p = 0; draw()"); check(/ECU maps below/.test(el("editor").innerHTML), "ECU map: points to the maps");
  R("edit(1)"); e = el("editor").innerHTML;
  check(/<option value="25" selected>GPIO 25<\/option>/.test(e) && /<option value="2" selected>Center/.test(e), "editing button 2: its own pin is pickable, its screen selected");
  console.log("3. a ladder button");
  R("edit(-1,2)"); await settle(); e = el("editor").innerHTML;
  check(/<option value="2" selected>On a resistor ladder/.test(e) && /<option value="140" selected>ADS1115 #1 A0 - Button ladder \(Map, Mode\)/.test(e), "Add a ladder button: on the same pin as the others (shared, not greyed)");
  const ps = e.split('id="b_pin"')[1].split("</select>")[0];
  check(/value="141">ADS1115 #1 A1<\/option>/.test(ps) && /value="34">GPIO 34/.test(ps) && /value="36" disabled>GPIO 36 - used by Fuel/.test(ps) && !/value="12"/.test(ps) && !/value="100"/.test(ps), "analog pins only; the fuel sender's greyed out");
  check(/Learn \(hold the button\)/.test(e) && gets.some(u => u === "/buttons/read?pin=140"), "Learn, and the pin is read live");
  check(/1\.650 V/.test(el("b_now").textContent), "shows the pin now");
  el("b_pin").value = "140";   // (the fake page keeps the last value set; a real select follows the page)
  await R("learn()"); await settle();
  check(el("b_mv").value === 1650 && /Learned 1650 mV/.test(el("msg").textContent) && el("msg").className === "", "Learn: takes the reading");
  reading = "1100"; await R("learn()"); await settle();
  check(/that is button 4 \(Mode\)/.test(el("msg").textContent) && el("msg").className === "bad", "Learn near another button on the ladder: warned");
  reading = "1650"; await R("learn()"); await settle();
  R("ed.a = 10; draw()"); el("b_name").value = "Lap"; el("b_pin").value = "140"; el("b_mv").value = "1650";
  await R("saveB()"); await settle();
  q = lastQ("/buttons/set?");
  check(q.get("kind") === "2" && q.get("pin") === "140" && q.get("mv") === "1650" && q.get("a") === "10" && q.get("t") === "0" && q.get("high") === "0" && q.get("name") === "Lap", "saved: ladder on A0 at 1650 mV, log marker");
  reply = { ok: false, t: "Too close to button 4" };
  R("edit(-1,2)"); await settle(); await R("saveB()"); await settle();
  check(el("msg").textContent === "Too close to button 4" && el("msg").className === "bad" && el("editor").innerHTML !== "", "the hub's refusal shows in red; the editor stays");
  reply = { ok: true, t: "Saved" };
  R("ed = null; draw()"); check(el("editor").innerHTML === "", "Cancel closes the editor");
  await R("clearB(2)"); await settle(); check(gets.includes("/buttons/clear?i=2"), "Remove");
  console.log("4. ECU maps");
  let m = el("ecu").innerHTML;
  check(/value="0x7F1"/.test(m) && /<option value="2" selected>2/.test(m) && /Map 2 name \(in use\)/.test(m) && /value="Street"/.test(m) && !/m_n2/.test(m), "id in hex, 2 maps named, map 2 in use");
  R("keepEcu(); ecuDraft.count = 4; renderEcu()"); m = el("ecu").innerHTML;
  check(/m_n3/.test(m) && !/m_n4/.test(m), "4 maps: 4 name boxes");
  el("m_id").value = "0x6A0"; el("m_n0").value = "Valet"; el("m_n1").value = "Street"; el("m_n2").value = "Boost"; el("m_n3").value = "";
  await R("saveEcu()"); await settle();
  q = lastQ("/buttons/ecu?");
  check(q.get("id") === "0x6a0" && q.get("count") === "4" && q.get("n0") === "Valet" && q.get("n2") === "Boost" && q.get("n4") === "", "saved: id 0x6A0, 4 maps by name");
  console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})();
