// Drives the /log-channels ("Logging") page script with a fake board: the start condition section and the
// channel ticks (which used to tick the NEXT signal: channels[id] instead of channels[index]).
const vm = require('vm');
const script = require('./extract_page.js').pageScript('LOGCHANS_HTML');
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };
const settle = async () => { for (let i = 0; i < 10; i++) await new Promise(r => setImmediate(r)); };

const els = {};
const el = id => els[id] || (els[id] = { id, textContent: "", innerHTML: "", value: "", style: {} });
const posts = [];
const board = { mode: 1, sig: 9, above: true, level: 90, stop_delay_s: 10,
  signals: [{ id: 1, name: "RPM" }, { id: 9, name: "Throttle Position" }, { id: 25, name: "Speed" }] };
const ctx = {
  document: { getElementById: el }, window: {}, URLSearchParams, setInterval: () => 0, console,
  fetch: async (url, o) => {
    if (o && o.method === "POST") { posts.push({ url, body: o.body }); return { ok: url !== "/log-trigger" || !/mode=3&sig=0/.test(o.body), text: async () => "Pick a sensor and type a level" }; }
    if (url === "/log-channels.json") return { json: async () => [{ id: 1, name: "RPM", enabled: true }, { id: 2, name: "Coolant Temp", enabled: false }] };
    if (url === "/log-trigger.json") return { json: async () => board };
    if (url === "/status.json") return { json: async () => ({ logging: "Logging to log_7.csv" }) };
    throw new Error(url);
  } };
vm.createContext(ctx); vm.runInContext(script + ";this.__ch=()=>channels;", ctx);

(async () => {
  await settle();
  console.log("1. loads");
  check(el("now").textContent === "Logging to log_7.csv", "shows what logging is doing: " + el("now").textContent);
  check(el("tmode").value === "1" && el("tsig").value === "9" && el("tabove").value === "1" && el("tlevel").value === 90 && el("tstop").value === 10, "the saved start condition fills the form");
  check(/Throttle Position/.test(el("tsig").innerHTML) && /Speed/.test(el("tsig").innerHTML), "the sensor list comes from the board");
  check(el("tsigbox").style.display === "none", "the sensor / level boxes are hidden unless 'a sensor' is chosen");
  el("tmode").value = "3"; vm.runInContext("showTrig()", ctx);
  check(el("tsigbox").style.display === "", "and shown when it is");

  console.log("2. the channel ticks change the RIGHT signal");
  check(/channels\[0\]\.enabled/.test(el("channels").innerHTML) && /channels\[1\]\.enabled/.test(el("channels").innerHTML) && !/channels\[2\]/.test(el("channels").innerHTML),
    "box 1 changes channels[0] (RPM), box 2 changes channels[1] (Coolant) - not the next one along");

  console.log("3. saving the start condition");
  el("tsig").value = "9"; el("tabove").value = "1"; el("tlevel").value = "90"; el("tstop").value = "15";
  await vm.runInContext("saveTrig()", ctx);
  const p = posts.find(x => x.url === "/log-trigger");
  check(p && p.body === "mode=3&sig=9&above=1&level=90&stop=15", "posts mode, sensor, above, level and stop delay: " + (p && p.body));
  check(el("tmsg").textContent === "Saved.", "says Saved");
  el("tsig").value = "0"; await vm.runInContext("saveTrig()", ctx);
  check(el("tmsg").textContent === "Not saved: Pick a sensor and type a level", "the board's reason is shown when it refuses: " + el("tmsg").textContent);

  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})();
