// Drives the HUB's /simulator page script (pulled straight out of esp32-input/hub_pages.h) with a fake hub.
// The simulator moved from the screens to the hub in screen build 162 / hub build 15.
const fs = require('fs');
const path = require('path');
const script = require('./hub_page.js').hubScript('SIM_HTML');

// ---- fake browser ----
const timers = [];
global.setInterval = (fn, ms) => { timers.push(fn); return 1; };
const els = {}; let htmlSets = {};
const getEl = id => {
  if (!els[id]) {
    const e = { id, textContent: "", className: "", value: "", _html: "" };
    Object.defineProperty(e, "innerHTML", { get() { return this._html; }, set(v) { this._html = v; htmlSets[id] = (htmlSets[id] || 0) + 1; } });
    els[id] = e;
  }
  return els[id];
};
global.document = { getElementById: id => {
  // elements the generated slider markup would have created
  const m = /^[vr](\d+)$/.exec(id);
  if (m && !els[id] && !els.sliders._html.includes('id="' + id + '"')) return null;
  return getEl(id);
}, activeElement: null };
getEl("sliders");

// ---- fake board ----
const SIGS = ["RPM","Coolant Temp","MAP/Boost","AFR","Oil Pressure","Oil Temp","Fuel Pressure","Ethanol %","Throttle Position","Battery Voltage"];
const board = { active: false, manual: false, seconds_left: 0, stop_reason: "", values: SIGS.map((_, i) => 100 + i), posts: [], inFlight: 0, maxInFlight: 0, failNext: null };
function boardJson() {
  return { active: board.active, manual: board.manual, seconds_left: board.seconds_left, stop_reason: board.stop_reason,
    signals: SIGS.map((n, i) => ({ id: i + 1, name: n, min: 0, max: 9000, step: i === 3 ? 0.1 : 1, value: board.values[i] })) };
}
global.URLSearchParams = URLSearchParams;
global.fetch = async (url, opts) => {
  if (opts && opts.method === "POST") {
    board.inFlight++; board.maxInFlight = Math.max(board.maxInFlight, board.inFlight);
    await new Promise(r => setImmediate(r));
    board.inFlight--;
    const p = Object.fromEntries(new URLSearchParams(opts.body));
    board.posts.push(p);
    if (board.failNext) { const f = board.failNext; board.failNext = null; return { ok: false, status: f.status, text: async () => f.text }; }
    if (p.cmd === "start") { board.active = true; board.manual = p.mode === "manual"; board.seconds_left = 600; board.stop_reason = ""; }
    if (p.cmd === "stop") { board.active = false; board.stop_reason = "stopped from the web page"; }
    if (p.cmd === "set") board.values[p.sig - 1] = Number(p.val);
    return { ok: true, status: 200, text: async () => "OK" };
  }
  return { json: async () => boardJson() };
};
const tick = () => new Promise(r => setImmediate(r));
const settle = async () => { for (let i = 0; i < 6; i++) await tick(); };
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };

eval(script + "\n;global.__refresh = refresh; global.__cmd = cmd; global.__slide = slide;");

(async () => {
  await settle();
  console.log("1. board is off");
  check(/^Off$/.test(els.state.textContent) && els.state.className === "statebox", "shows Off: " + els.state.textContent);
  check(els.sliders._html === "", "no sliders while off");

  console.log("2. last stop reason is shown");
  board.stop_reason = "real CAN traffic arrived"; await __refresh();
  check(/last stopped: real CAN traffic arrived/.test(els.state.textContent), "reason shown: " + els.state.textContent);

  console.log("3. start demo");
  await __cmd("start", "demo"); await settle();
  check(board.posts.at(-1).cmd === "start" && board.posts.at(-1).mode === "demo", "posted start + demo");
  check(/SIMULATING \(demo\) - switches off in 10:00/.test(els.state.textContent) && els.state.className === "statebox on", "running banner and countdown: " + els.state.textContent);
  check((els.sliders._html.match(/<input/g) || []).length === 10 && (els.sliders._html.match(/disabled/g) || []).length === 10, "ten read-only sliders in demo mode");
  board.seconds_left = 599; await __refresh();
  check(/switches off in 9:59/.test(els.state.textContent), "countdown format: " + els.state.textContent);
  board.seconds_left = 65; await __refresh();
  check(/switches off in 1:05/.test(els.state.textContent), "seconds are zero-padded: " + els.state.textContent);
  const builds = htmlSets.sliders;
  board.values[0] = 4321; await __refresh(); await __refresh();
  check(htmlSets.sliders === builds, "sliders are NOT rebuilt while the mode is unchanged (so dragging is never interrupted)");
  check(els.v1.textContent === "4321" && els.r1.value === 4321, "live value follows the board");

  console.log("4. manual mode");
  await __cmd("stop"); await __cmd("start", "manual"); await settle();
  check(/SIMULATING \(manual\)/.test(els.state.textContent), "manual running");
  check((els.sliders._html.match(/disabled/g) || []).length === 0, "sliders enabled");
  check(els.v4.textContent === "103.0" || els.v4.textContent === "103", "AFR slider shows one decimal place: " + els.v4.textContent);

  console.log("5. dragging a slider sends ONE request at a time, newest value wins");
  board.maxInFlight = 0; board.posts.length = 0;
  for (let v = 1000; v <= 3000; v += 100) __slide(1, String(v));      // a fast drag: 21 changes
  await settle(); await settle();
  check(board.maxInFlight === 1, "never more than one request in flight (max was " + board.maxInFlight + ")");
  check(board.posts.length < 21 && board.posts.length >= 1, "a fast drag does not send a request per pixel (" + board.posts.length + " sent)");
  check(board.posts.at(-1).cmd === "set" && board.posts.at(-1).sig === "1" && board.posts.at(-1).val === "3000", "the last request carries the final position");
  check(board.values[0] === 3000, "the board ended up with the final value");

  console.log("6. errors from the board are shown");
  board.failNext = { status: 409, text: "Real CAN frames are arriving (412 per second), so the simulator will not start. It only runs while the bus is silent." };
  await __cmd("start", "demo"); await settle();
  check(/Real CAN frames are arriving \(412 per second\)/.test(els.msg.textContent) && els.msg.className === "bad", "409 text shown in red: " + els.msg.textContent);
  board.failNext = { status: 409, text: "The simulator is not running in manual mode" };
  __slide(1, "500"); await settle(); await settle();
  check(/not running in manual mode/.test(els.msg.textContent), "set error shown");

  console.log("7. stop");
  await __cmd("stop"); await settle();
  check(/Off - last stopped: stopped from the web page/.test(els.state.textContent) && els.sliders._html === "", "off again, sliders gone: " + els.state.textContent);

  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})();
