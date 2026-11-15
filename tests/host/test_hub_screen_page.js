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
  console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})();
