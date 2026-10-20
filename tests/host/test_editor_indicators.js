// The indicators-page editor (web_indicator_editor.h), driven in BOTH editors that include it: the display editor and the web dashboard editor.
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const { pageScript } = require('./extract_page.js');
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };

const ENUM = (() => {            // the SignalId enum in gauge_config.h, in order
  const h = fs.readFileSync(path.join(__dirname, '..', '..', 'esp32s3-amoled-bsp-base', 'gauge_config.h'), 'utf8');
  const body = h.match(/typedef enum \{\s*SIG_NONE = 0,([\s\S]*?)SIG_COUNT\s*\} SignalId;/)[1];
  return ['SIG_NONE', ...[...body.matchAll(/\b(SIG_[A-Z0-9]+)\b/g)].map(m => m[1])];
})();
const NAMES = ["None", "RPM", "Coolant Temp", "MAP/Boost", "AFR", "Oil Pressure", "Oil Temp", "Fuel Pressure", "Ethanol %", "Throttle Position", "Battery Voltage",
  "Rev Limiter Active", "Main Relay", "Fuel Pump", "Check Engine Light", "O2 Heater", "Lambda Protection", "Fan 1", "Fan 2", "Brake Pedal"];

function makeEditor(pageName, postUrl, getUrl) {
  const els = {}; const posts = [];
  const box = {
    document: { getElementById: id => els[id] || (els[id] = { id, innerHTML: "", textContent: "", value: "" }) },
    confirm: () => true, console, setTimeout, Date, JSON, Math, Number, String, parseInt, parseFloat, isNaN, Object, Array,
    fetch: async (url, opts) => {
      if (opts && opts.method === "POST") { posts.push({ url, body: opts.body }); return { ok: true, status: 200, text: async () => "OK" }; }
      if (url === getUrl) return { json: async () => [{ enabled: true, layout: 0, cells: [{ enabled: true, signal: 2, gauge_type: 0, min: 0, max: 250, rest: 0, caption: "CLT", needle_color: 16777215, tick_color: 16777215, bg_color: 0, value_divisor: 1, tick_count: 26, major_nth: 5, refresh_hz: 10, show_value: false, value_font_size: 28, has_warning: false, warning_threshold: 0, warning_above: false }] }] };
      if (url === "/signals.json") return { json: async () => NAMES.map((n, i) => ({ id: i, name: n, age_ms: i === 0 ? 4294967295 : 100 * i, flag: i >= 11 })) };
      throw new Error("unexpected fetch " + url);
    },
  };
  box.window = box;
  const ctx = vm.createContext(box);
  vm.runInContext(pageScript(pageName), ctx);
  const run = code => vm.runInContext(code, ctx);
  return { ctx, run, els, posts };
}
const settle = async () => { for (let i = 0; i < 10; i++) await new Promise(r => setImmediate(r)); };

(async () => {
  console.log("0. CONTRACT: the editor's signal numbers are the firmware's SignalId order");
  const probe = makeEditor('GAUGES_HTML', '/gauges', '/gauges.json'); await settle();
  const SIGMAP = probe.run('SIG');
  let ok = true; for (const [k, v] of Object.entries(SIGMAP)) if (ENUM[v] !== 'SIG_' + k) { ok = false; console.log("   mismatch:", k, v, ENUM[v]); }
  check(ok && Object.keys(SIGMAP).length === ENUM.length - 1, "every JS signal number matches its enum position in gauge_config.h (" + Object.keys(SIGMAP).length + " signals)");
  check(ENUM.length === 26 && ENUM[25] === 'SIG_VSS' && ENUM[11] === 'SIG_REVLIM' && ENUM[19] === 'SIG_BRAKE' && ENUM[20] === 'SIG_EGT' && ENUM[21] === 'SIG_IAT' && ENUM[22] === 'SIG_AMBT' && ENUM[23] === 'SIG_HUM' && ENUM[24] === 'SIG_FUEL', "the enum has the ten analog signals, the nine flags, then EGT and IAT (appended so saved numbers keep their meaning)");
  const presets = probe.run('IND_PRESETS'); const unitMap = probe.run('SIGNAL_UNITS');
  check(presets.length === 18 && new Set(presets.map(p => p.name)).size === 18, "18 presets with unique names");
  check(presets.every(p => p.signal >= 1 && p.signal <= 19 && (p.op === 0 || p.op === 1) && Number.isFinite(p.threshold) && p.text.length <= 11 && /^[\x20-\x7E]+$/.test(p.text)), "every preset: a real signal, a valid condition, text of at most 11 printable ASCII characters (what the display accepts)");
  check(presets.filter(p => p.signal >= 11).every(p => p.op === 0 && p.threshold === 0.5 && !p.hyst), "ECU-flag presets are 'above 0.5' with no hysteresis (a flag is 1 or 0)");
  check(presets.filter(p => p.signal <= 10).every(p => unitMap[p.signal]), "every analog signal used by a preset has a unit shown in the editor");
  check(probe.run('IND_DEFAULT_PAGE').every(n => presets.some(p => p.name === n)), "the default page's six lamps all exist as presets");

  for (const [label, pageName, postUrl, getUrl] of [["display editor", 'GAUGES_HTML', '/gauges', '/gauges.json']]) {
    console.log(`\n=== ${label} ===`);
    const E = makeEditor(pageName, postUrl, getUrl); await settle();
    const R = E.run;
    check(R('pages.length') === 1 && R('signalActivity.length') === 20 && R('signalActivity.filter(s => s.flag).length') === 9, "loads its pages and the 20 signals, 9 of them flags");
    check(!R('signalOptionsHtml(0)').includes("Fan 1") && R('signalOptionsHtml(0)').includes("Coolant Temp"), "a GAUGE's signal list has no ECU flags (they are for indicators)");

    console.log("1. + Add Indicators Page");
    R('addIndicatorPage()');
    check(R('pages.length') === 2 && R('pages[1].kind') === 1 && R('pages[1].cols') === 2 && R('pages[1].indicators.length') === 6 && R('pages[1].enabled') === true, "adds an indicators page: 2 columns, 6 lamps");
    const L = R('pages[1].indicators');
    check(L.map(l => l.on_text).join("|") === "CHECK ENG|OIL|TEMP|BATT|REV LIM|FAN 1", "starting lamps: check engine, oil, temperature, battery, rev limiter, fan: " + L.map(l => l.on_text).join("|"));
    check(L[0].signal === 14 && L[0].op === 0 && L[0].threshold === 0.5 && L[0].on_color === 0xFFB300 && L[0].on_text_color === 0x000000, "check engine: an ECU flag, amber with black text (readable)");
    check(L[1].signal === 5 && L[1].op === 1 && L[1].threshold === 10 && L[1].hysteresis === 2 && L[1].on_color === 0xE53935, "oil: oil pressure BELOW 10 psi, hysteresis 2, red");
    check(L[4].blink === true && L[0].blink === false, "the rev limiter flashes, the others do not");
    check(L.every(l => l.off_color === 0x2A2A2A && l.off_text_color === 0x808080 && l.off_text === l.on_text), "every lamp has a dark off look that still shows its label, dimly");

    console.log("2. what is shown");
    const html = R('renderIndicatorPage(1, pages[1])');
    check(html.includes('class="badge">Indicators</span>') && (html.match(/class="cell lamp-card"/g) || []).length === 6 && (html.match(/class="chip"/g) || []).length === 12, "an Indicators badge, six lamp cards, an ON and an OFF preview chip on each");
    check(html.includes('<option value="2" selected>2 columns</option>') && html.includes('4 columns'), "the columns choice, with 2 selected");
    check(html.includes("Lamp is on while the ECU flag is") && (html.match(/Lamp is on while the ECU flag is/g) || []).length === 3, "flag lamps (check engine, rev limiter, fan) say 'on while the ECU flag is On/Off' instead of a threshold");
    check(html.includes("Threshold (psi)") && html.includes("Threshold (F)") && html.includes("Threshold (V)"), "analog lamps show their threshold with the signal's own unit (psi, deg F, V)");
    check(html.includes('background:#e53935;color:#ffffff') && html.includes('background:#2a2a2a;color:#808080'), "the preview chips use the lamp's real colours");
    check(html.includes("12 is the most") === false && !/addlamp[^>]*disabled/.test(html), "with 6 lamps the add button is active");
    check(R('indicatorSignalOptions(14)').includes('<optgroup label="ECU on/off flags">') && R('indicatorSignalOptions(14)').includes('<option value="14" selected>Check Engine Light'), "the lamp signal list groups the ECU flags and selects the right one");
    check(R('indicatorSignalOptions(0)').includes("Oil Pressure [psi]"), "analog entries show their unit in brackets");
    R('renderPages()');
    check(R('document.getElementById("pages").innerHTML').includes("Page 1") && R('document.getElementById("pages").innerHTML').includes("Page 2") && R('document.getElementById("pages").innerHTML').includes("lamp-card") && R('document.getElementById("pages").innerHTML').includes("page-header"), "renderPages draws the gauge page and the indicators page together, in order");

    console.log("3. editing a lamp");
    R('applyPreset(1, 0, "6")');           // preset 6 = Boost
    check(R('pages[1].indicators[0].signal') === 3 && R('pages[1].indicators[0].on_text') === "BOOST" && R('pages[1].indicators[0].on_color') === 0x1E88E5 && R('pages[1].indicators.length') === 6, "a preset replaces one lamp's settings and nothing else");
    R('applyPreset(1, 0, "")'); R('applyPreset(1, 0, "99")'); check(R('pages[1].indicators[0].on_text') === "BOOST", "choosing nothing, or a bad preset number, changes nothing");
    R('setLampSignal(1, 0, 17)');          // Fan 1
    check(R('pages[1].indicators[0].signal') === 17 && R('pages[1].indicators[0].op') === 0 && R('pages[1].indicators[0].threshold') === 0.5 && R('pages[1].indicators[0].hysteresis') === 0, "picking an ECU flag sets 'on while the flag is on' (above 0.5)");
    R('setFlagMode(1, 0, false)');
    check(R('pages[1].indicators[0].op') === 1 && R('pages[1].indicators[0].threshold') === 0.5, "'on while the flag is OFF' is 'below 0.5'");
    R('setLampSignal(1, 0, 5)');
    check(R('pages[1].indicators[0].signal') === 5 && R('pages[1].indicators[0].threshold') === 0, "switching from a flag back to an analog signal clears the leftover 0.5");
    R('setLampNumber(1, 0, "threshold", "37.5")'); R('setLampSignal(1, 0, 6)');
    check(R('pages[1].indicators[0].threshold') === 37.5, "a threshold you typed is kept when you change the signal");
    R('setLampNumber(1, 0, "threshold", "abc")'); R('setLampNumber(1, 0, "threshold", "")'); check(R('pages[1].indicators[0].threshold') === 37.5, "non-numeric input changes nothing");
    R('setLampNumber(1, 0, "hysteresis", "-4")'); check(R('pages[1].indicators[0].hysteresis') === 0, "a negative hysteresis becomes 0");
    R('setLampField(1, 0, "op", 1)'); check(R('pages[1].indicators[0].op') === 1, "above / below");
    R('setLampField(1, 0, "blink", true)'); check(R('pages[1].indicators[0].blink') === true, "flash while on");
    E.els['chips_1_0'] = { innerHTML: "" };
    R('setLampText(1, 0, "on_text", "A<b>&")');
    check(R('pages[1].indicators[0].on_text') === "A<b>&" && E.els['chips_1_0'].innerHTML.includes("A&lt;b&gt;&amp;") && !E.els['chips_1_0'].innerHTML.includes("<b>"), "typed text updates the preview chip, HTML-escaped (a < or & cannot break the page)");
    check(R('renderLamp(1, 0, pages[1].indicators[0], 6)').includes('value="A&lt;b&gt;&amp;"'), "and is escaped inside the text box too");
    R('setLampColor(1, 0, "on_color", "#12ab9f")'); check(R('pages[1].indicators[0].on_color') === 0x12AB9F && E.els['chips_1_0'].innerHTML.includes("#12ab9f"), "a colour picked in the page becomes a number, and the chip follows");
    R('setLampColor(1, 0, "off_text_color", "#ffffff")'); check(R('pages[1].indicators[0].off_text_color') === 0xFFFFFF, "all four colours can be set");

    console.log("4. adding, removing, ordering");
    R('setIndCols(1, 9)'); check(R('pages[1].cols') === 4, "columns 9 -> 4"); R('setIndCols(1, 0)'); check(R('pages[1].cols') === 2, "columns 0 -> the default 2"); R('setIndCols(1, 3)'); check(R('pages[1].cols') === 3, "3 columns");
    R('addLamp(1)'); check(R('pages[1].indicators.length') === 7 && R('pages[1].indicators[6].signal') === 0 && R('pages[1].indicators[6].on_text') === "", "'+ Add indicator' appends a blank lamp with no signal chosen");
    for (let i = 0; i < 10; i++) R('addLamp(1)');
    check(R('pages[1].indicators.length') === 12, "12 is the most");
    check(R('renderIndicatorPage(1, pages[1])').includes("(12 is the most)") && /addlamp"[^>]*disabled/.test(R('renderIndicatorPage(1, pages[1])')), "at 12 the add button is greyed out and says so");
    R('removeLamp(1, 11)'); R('removeLamp(1, 10)'); R('removeLamp(1, 9)'); R('removeLamp(1, 8)'); R('removeLamp(1, 7)'); R('removeLamp(1, 6)'); check(R('pages[1].indicators.length') === 6, "remove");
    const first = R('pages[1].indicators[0].on_text'), second = R('pages[1].indicators[1].on_text');
    R('moveLamp(1, 0, 1)'); check(R('pages[1].indicators[0].on_text') === second && R('pages[1].indicators[1].on_text') === first, "move down swaps with the next lamp");
    R('moveLamp(1, 0, -1)'); R('moveLamp(1, 5, 1)'); check(R('pages[1].indicators.length') === 6 && R('pages[1].indicators[0].on_text') === second, "moving past either end does nothing");
    const h2 = R('renderIndicatorPage(1, pages[1])'); check(/moveLamp\(1,0,-1\)" disabled/.test(h2) && /moveLamp\(1,5,1\)" disabled/.test(h2), "the first lamp's up button and the last lamp's down button are greyed out");

    console.log("5. page actions");
    R('pageAction(1, "copy")');
    check(R('pages.length') === 3 && R('pages[2].kind') === 1 && R('pages[2].indicators.length') === 6, "copy duplicates an indicators page");
    R('pages[2].indicators[0].on_text = "COPY"'); check(R('pages[1].indicators[0].on_text') !== "COPY", "the copy is independent of the original (a deep copy)");
    R('pageAction(2, "empty")'); check(R('pages[2].kind') === 1 && R('pages[2].indicators.length') === 0 && R('pages[2].cols') === 3, "empty removes the lamps but keeps it an indicators page, with its columns");
    check(R('renderIndicatorPage(2, pages[2])').includes("+ Add indicator"), "an empty page still offers '+ Add indicator'");
    R('pageAction(2, "delete")'); check(R('pages.length') === 2, "delete");
    R('pageAction(0, "empty")'); check(R('pages[0].cells.length') >= 1 && R('pages[0].kind') !== 1, "'empty' on a gauge page still empties its cells (unchanged)");

    console.log("6. saving");
    R('loaded = true'); E.posts.length = 0;
    await R('save()');
    const sent = JSON.parse(E.posts[0].body);
    check(E.posts[0].url === postUrl && sent.length === 2, `posts to ${postUrl}`);
    check(sent[1].kind === 1 && sent[1].cols === 3 && sent[1].indicators.length === 6 && sent[1].layout === undefined && sent[1].cells === undefined, "an indicator page is sent as kind 1 with its columns and lamps, and no layout or cells");
    const s0 = sent[1].indicators[0];
    check(["signal", "op", "threshold", "hysteresis", "on_text", "off_text", "on_color", "off_color", "on_text_color", "off_text_color", "blink"].every(k => k in s0), "every lamp field is sent");
    check(sent[0].kind === undefined && sent[0].cells.length === 1, "the gauge page is sent as before");
  }

  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})();
