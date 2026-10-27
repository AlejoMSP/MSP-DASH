// The hub's home page (hub_pages.h HOME_HTML) with a fake hub: screens with their links, the status grid, and the
// navigation bar on every hub page.
const vm = require('vm'), { hubPage, hubScript } = require('./hub_page.js');
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };
const settle = async () => { for (let i = 0; i < 20; i++) await new Promise(r => setImmediate(r)); };
const els = {}; const el = id => els[id] || (els[id] = { innerHTML: "", textContent: "", value: "" });
const state = { lightsV: 4.5, lightsOn: false, dimV: 4.5, bri: 100, build: 30, ip: "192.168.55.204", ch: 10, last: "brightness 100", ecu: "rusEFI",
  live: 25, sent: 7, sim: "off", dht: "77 F, 40% humidity", fuel: 38, egt: "26 C / 78 F", egtsent: 5, cfg: { mode: 1, day: 100, nmin: 20, nmax: 60, dlo: 2, dhi: 12, lon: 6 } };
const screens = [{ name: "Right", online: true, ip: "192.168.55.233", build: 185, mac: "80:45:6B:33:94:68", age: 100 },
                 { name: "Center", online: true, ip: "192.168.55.231", build: 185, mac: "80:45:6B:33:96:CC", age: 100 },
                 { name: "Left", online: false, ip: "192.168.55.232", build: 184, mac: "80:45:6B:33:94:40", age: 90000 }];
const ctx = { document: { getElementById: el }, setInterval: () => 0, console,
  fetch: async u => ({ json: async () => u === "/state" ? state : u === "/screens.json" ? screens : u === "/speed.json" ? { ecuok: true, ecumph: 0, gpsok: true, fix: false, sats: 3 } : null, text: async () => "" }) };
vm.createContext(ctx); vm.runInContext(hubScript('HOME_HTML'), ctx);
(async () => {
  await settle();
  const s = el("scr").innerHTML;
  check(s.indexOf("Left") < s.indexOf("Center") && s.indexOf("Center") < s.indexOf("Right"), "screens listed Left, Center, Right");
  for (const [ip, p] of [["192.168.55.231", "/gauges"], ["192.168.55.231", "/log-channels"], ["192.168.55.232", "/settings"], ["192.168.55.233", "/sd"], ["192.168.55.233", "/"]])
    check(s.includes('href="http://' + ip + p + '"'), "link to " + ip + p);
  check(/OFFLINE, last heard 2 min ago/.test(s), "an offline screen says when it was last heard");
  const st = el("stat").innerHTML;
  check(/ECU<\/b>rusEFI/.test(st) && /Signals live<\/b>25/.test(st) && /build 30/.test(st), "status grid: ECU, signals, hub build");
  check(/77 F/.test(el("sens").innerHTML) && /38 %/.test(el("sens").innerHTML), "hub sensors: ambient and fuel");
  check(/100 % now/.test(el("brisum").textContent), "brightness folded away, with a one-line summary");
  for (const p of ["HOME_HTML", "SENSORS_HTML", "SIM_HTML", "CUSTOM_HTML", "RF_HTML", "BUTTONS_HTML"]) {
    const h = hubPage(p);
    check(/<div class="nav">/.test(h) && /href="\/buttons"/.test(h) && /\.nav\{/.test(h), p + " has the navigation bar");
  }
  console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})();
