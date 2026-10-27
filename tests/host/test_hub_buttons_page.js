// The hub's /buttons page (esp32-input/hub_pages.h) with a fake hub: the 6 x 3 grid, the page / colour choices, saving.
const fs = require('fs'), path = require('path'), vm = require('vm');
const script = require('./hub_page.js').hubScript('BUTTONS_HTML');
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };
const settle = async () => { for (let i = 0; i < 20; i++) await new Promise(r => setImmediate(r)); };
const actions = ["Nothing", "Next page", "Previous page", "Next gauge (choose screen)", "Go to page", "Dim all", "Brighten all", "Color scheme", "Race mode", "Clear warning", "Log marker"];
const bind = [0, 1, 2].map(() => [0, 1, 2, 3, 4, 5].map(() => [0, 0]));
bind[0][0] = [2, 0]; bind[1][0] = [1, 0]; bind[1][1] = [3, 0]; bind[0][1] = [9, 0]; bind[2][0] = [10, 0];
const els = {}; const el = id => els[id] || (els[id] = { innerHTML: "", textContent: "", className: "" });
const gets = []; let reply = { ok: true, t: "Saved" };
const ctx = { document: { getElementById: el }, URLSearchParams, console,
  fetch: async u => { gets.push(u);
    if (u === "/buttons.json") return { json: async () => ({ actions, colors: ["Classic", "Racing orange"], race: false, target: "", bind }) };
    return { ok: reply.ok, text: async () => reply.t }; } };
vm.createContext(ctx); vm.runInContext(script, ctx);
(async () => {
  await settle();
  const g = el("grid").innerHTML;
  check((g.match(/<tr>/g) || []).length === 6 && (g.match(/<td>/g) || []).length === 18, "6 rows (press, 1, 2, 3, 5, 10 s) x 3 columns (left, right, both)");
  check(/value="2" selected>Previous page/.test(g) && /value="10" selected>Log marker/.test(g), "the saved bindings are selected");
  check(/buttons control: all screens/.test(el("state").textContent), "shows which screens the buttons control");
  await vm.runInContext('pickA(1,4,8)', ctx); await settle();
  let q = new URLSearchParams(gets.pop().split("?")[1]);
  check(q.get("in") === "1" && q.get("lv") === "4" && q.get("a") === "8" && q.get("p") === "1", "race mode on right 5 s: saved with race page 1");
  check(/Race page 1/.test(el("grid").innerHTML), "a race page box appears");
  await vm.runInContext('pickA(2,2,7)', ctx); await settle();
  q = new URLSearchParams(gets.pop().split("?")[1]);
  check(q.get("a") === "7" && q.get("p") === "255", "color scheme: starts as 'next scheme each time'");
  check(/2\. Racing orange/.test(el("grid").innerHTML), "the colour box lists the schemes");
  await vm.runInContext('setB(2,2,7,1)', ctx); await settle();
  q = new URLSearchParams(gets.pop().split("?")[1]);
  check(q.get("p") === "1" && /saved/.test(el("msg").textContent), "picking scheme 2 saves param 1");
  reply = { ok: false, t: "Page must be 1 to 8" };
  await vm.runInContext('setB(0,3,4,9)', ctx); await settle();
  check(el("msg").textContent === "Page must be 1 to 8" && el("msg").className === "bad", "the hub's refusal shows in red");
  console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})();
