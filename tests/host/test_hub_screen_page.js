// The hub's /screen page (hub_pages.h SCREEN_HTML): one screen's own pages inside the hub's site, Left/Center/Right
// picker, tabs, the address kept in the URL. Also the /firmware page's board table.
const vm = require('vm'), { hubScript } = require('./hub_page.js');
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };
const settle = async () => { for (let i = 0; i < 20; i++) await new Promise(r => setImmediate(r)); };
const screens = [{ name: "Right", online: true, ip: "192.168.55.203", build: 194 }, { name: "Center", online: true, ip: "192.168.55.202", build: 194 },
                 { name: "Left", online: false, ip: "192.168.55.201", build: 193 }];
function page(search) {
  const els = {}; const el = id => els[id] || (els[id] = { innerHTML: "", textContent: "", src: "" });
  const hist = [];
  const ctx = { document: { getElementById: el }, URLSearchParams, encodeURIComponent, console, setInterval: () => 0,
    location: { search }, history: { replaceState: (a, b, u) => hist.push(u) },
    fetch: async u => ({ json: async () => u === "/screens.json" ? screens : { ip: "192.168.55.204", build: 40 } }) };
  vm.createContext(ctx); return { ctx, el, hist };
}
(async () => {
  console.log("1. screens");
  let p = page("?s=Center&p=log-channels"); vm.runInContext(hubScript('SCREEN_HTML'), p.ctx); await settle();
  const who = p.el("who").innerHTML;
  check(who.indexOf("Left") < who.indexOf("Center") && who.indexOf("Center") < who.indexOf("Right"), "picker in Left, Center, Right order");
  check(/class="on" onclick="pick\('Center'\)">Center</.test(who) && /Left \(offline\)/.test(who), "Center chosen, Left marked offline");
  check(p.el("fr").src === "http://192.168.55.202/log-channels" && /class="on"[^>]*>Logging</.test(p.el("tabs").innerHTML), "opens Center's logging page");
  vm.runInContext("pick('Right'); tab('gauges')", p.ctx);
  check(p.el("fr").src === "http://192.168.55.203/gauges" && p.hist.pop() === "/screen?s=Right&p=gauges", "switching screen and tab: frame and address follow");
  vm.runInContext("tab('')", p.ctx);
  check(p.el("fr").src === "http://192.168.55.203/" && /open on its own/.test(p.el("where").innerHTML), "Live tab, and a link to open the screen by itself");
  p = page(""); vm.runInContext(hubScript('SCREEN_HTML'), p.ctx); await settle();
  check(p.el("fr").src === "http://192.168.55.202/gauges" || p.el("fr").src === "http://192.168.55.201/gauges", "no choice in the address: the first screen's gauge editor");
  console.log("2. firmware");
  p = page(""); vm.runInContext(hubScript('FIRMWARE_HTML'), p.ctx); await settle();
  const b = p.el("b").innerHTML;
  check(/<b>Hub<\/b><\/td><td>192.168.55.204<\/td><td>40</.test(b) && /<b>Left<\/b><\/td><td>192.168.55.201<\/td><td>193<\/td><td><span class="chip bad">offline/.test(b), "every board with its build and status");
  check(/href="http:\/\/192.168.55.203\/update"/.test(b), "each screen's update page linked");
  console.log("3. updating the screens from one file");
  { const els = {}; const el = id => els[id] || (els[id] = { innerHTML: "", textContent: "", checked: false, files: [], style: {}, disabled: false });
    let builds = { "192.168.55.201": 193, "192.168.55.202": 194, "192.168.55.203": 194 }, online = { "192.168.55.201": false, "192.168.55.202": true, "192.168.55.203": true };
    const posts = [];
    const scr = () => [{ name: "Right", ip: "192.168.55.203" }, { name: "Center", ip: "192.168.55.202" }, { name: "Left", ip: "192.168.55.201" }].map(x => Object.assign({ online: online[x.ip], build: builds[x.ip] }, x));
    const ctx = { document: { getElementById: el }, console, setInterval: () => 0, setTimeout: (f) => setImmediate(f), confirm: () => true, alert: m => { throw new Error("alert: " + m); },
      FormData: class { append(k, v, n) { this.k = k; this.n = n; } },
      fetch: async (u, o) => {
        if (o && o.method === "POST") { posts.push([u, o.mode, o.body.k]); const ip = u.split("/")[2]; builds[ip] = 195; return {}; }
        return { json: async () => u === "/screens.json" ? scr() : { ip: "192.168.55.204", build: 41 } };
      } };
    vm.createContext(ctx); vm.runInContext(hubScript('FIRMWARE_HTML'), ctx); await settle();
    check(el("t_192.168.55.202").checked !== undefined && /id="t_192.168.55.201" disabled/.test(el("b").innerHTML), "online screens can be ticked, an offline one cannot");
    el("sbin").files = [{ name: "esp32s3-amoled-bsp-base.ino.bin" }];
    el("t_192.168.55.202").checked = true; el("t_192.168.55.203").checked = true;
    await vm.runInContext('updScreens()', ctx); await settle();
    check(posts.length === 2 && posts[0][0] === "http://192.168.55.202/update" && posts[1][0] === "http://192.168.55.203/update", "uploads go to the ticked screens, one after another (Center, then Right)");
    check(posts.every(p => p[1] === "no-cors" && p[2] === "firmware"), "sent as the 'firmware' form field, like the curl command");
    check(/done, build 195/.test(el("b").innerHTML), "each shows done with its new build once it is back"); }
  console.log("4. Setup page sections");
  { const h = require('./hub_page.js').hubPage('SETUP_HTML');
    const ids = [...h.matchAll(/<div class="card" id="(\w+)"/g)].map(m => m[1]);
    check(ids.join(",") === "brightness,clock,accel,i2c,speed,hub", "cards in order: brightness, clock, accelerometer, I2C, speed check, hub");
    check(["brightness", "clock", "accel", "i2c", "speed", "hub"].every(a => h.includes('href="#' + a + '"')), "a jump link to each"); }
  console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})();
