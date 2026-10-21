// Settings every gauge cell has in the gauge editor: the template (scale and warning) per signal,
// caption size, units text, gauge position (centre / top / bottom) and, in the display editor only, the caption on the dial face.
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const { pageScript } = require('./extract_page.js');
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };
const SK = path.join(__dirname, '..', '..', 'esp32s3-amoled-bsp-base');

const ENUM = (() => {
  const h = fs.readFileSync(path.join(SK, 'gauge_config.h'), 'utf8');
  const body = h.match(/typedef enum \{\s*SIG_NONE = 0,([\s\S]*?)SIG_COUNT\s*\} SignalId;/)[1];
  return ['SIG_NONE', ...[...body.matchAll(/\b(SIG_[A-Z0-9]+)\b/g)].map(m => m[1])];
})();
const STYLE = fs.readFileSync(path.join(SK, 'gauge_style.h'), 'utf8');
const NAMES = ["None", "RPM", "Coolant Temp", "MAP/Boost", "AFR", "Oil Pressure", "Oil Temp", "Fuel Pressure", "Ethanol %", "Throttle Position", "Battery Voltage",
  "Rev Limiter Active", "Main Relay", "Fuel Pump", "Check Engine Light", "O2 Heater", "Lambda Protection", "Fan 1", "Fan 2", "Brake Pedal"];

function makeEditor(pageName, getUrl) {
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
  return { ctx, run: code => vm.runInContext(code, ctx), els, posts };
}
const settle = async () => { for (let i = 0; i < 10; i++) await new Promise(r => setImmediate(r)); };

(async () => {
  console.log("0. CONTRACT with gauge_style.h (the firmware's own lists)");
  const probe = makeEditor('GAUGES_HTML', '/gauges.json'); await settle();
  { const block = STYLE.match(/#define GAUGE_DEFAULT_RANGES\(X\)([\s\S]*?)\n\n/)[1];
    const fw = {}; for (const m of block.matchAll(/X\((SIG_[A-Z0-9]+),\s*(-?\d+),\s*(-?\d+),\s*(\d),\s*(-?\d+),\s*(\d)\)/g)) fw[ENUM.indexOf(m[1])] = [+m[2], +m[3], +m[4], +m[5], +m[6]];
    const js = probe.run('JSON.stringify(SIGNAL_DEFAULT_RANGES)');
    check(Object.keys(fw).length === 16 && JSON.stringify(Object.fromEntries(Object.entries(fw).sort((a, b) => a[0] - b[0]))) === JSON.stringify(JSON.parse(js)), "the editor's gauge templates are EXACTLY the firmware's list: " + js);
    check(fw[4].join() === "10,20,0,0,1" && fw[2].join() === "40,300,1,230,1" && fw[10].join() === "8,16,1,12,0" && fw[5].join() === "0,100,1,10,0", "AFR 10-20; coolant 40-300 F warn above 230; battery 8-16 V warn below 12; oil pressure 0-100 psi warn below 10");
    check(fw[3].join() === "-15,30,0,0,1", "MAP is boost psi: -15 (vacuum) to 30");
    check(Object.keys(fw).every(k => !(+k >= 11 && +k <= 19)), "the ECU on/off flags have no template"); }
  { const names = [...probe.run('signalOptionsHtml(0)').matchAll(/<option value="(\d+)"[^>]*>([^<(]+)/g)].filter(m => m[1] !== "0").map(m => m[2].trim());
    check(names.length > 5 && names.every((n, i) => i === 0 || names[i - 1].localeCompare(n) <= 0), "the gauge Signal list is alphabetical, whatever has data: " + names.join(", "));
    const ind = [...probe.run('indicatorSignalOptions(0)').split("<optgroup")[0].matchAll(/<option value="(\d+)"[^>]*>([^<\[(]+)/g)].filter(m => m[1] !== "0").map(m => m[2].trim());
    check(ind.length > 5 && ind.every((n, i) => i === 0 || ind[i - 1].localeCompare(n) <= 0), "the lamp Signal list is alphabetical too"); }
  { const fwSizes = STYLE.match(/GAUGE_CAPTION_SIZES\[GAUGE_CAPTION_SIZE_COUNT\] = \{([^}]*)\}/)[1].split(",").map(s => +s.trim());
    const js = probe.run('CAPTION_SIZES.map(s => s[0])');
    check(JSON.stringify(js) === JSON.stringify([0, ...fwSizes]) && fwSizes.join() === "14,20,28,36,48", "the editor offers 'Normal' (0) and exactly the firmware's caption sizes: " + fwSizes.join(", ")); }
  { const d = n => +STYLE.match(new RegExp("#define GAUGE_VALIGN_" + n + "\\s+(\\d)"))[1];
    const js = probe.run('VALIGNS.map(v => v[0] + ":" + v[1]).join(",")');
    check(js === `${d("CENTER")}:Center,${d("TOP")}:Top,${d("BOTTOM")}:Bottom`, "Center / Top / Bottom use the firmware's numbers: " + js); }

  for (const [label, pageName, getUrl, display] of [["display editor", 'GAUGES_HTML', '/gauges.json', true]]) {
    console.log(`\n=== ${label} ===`);
    const E = makeEditor(pageName, getUrl); await settle(); const R = E.run;
    R('pages[0].cells[0].min = 5; pages[0].cells[0].max = 50; pages[0].cells[0].rest = 7;');

    console.log("1. choosing a signal starts it from its template");
    const scale = () => R('[pages[0].cells[0].signal, pages[0].cells[0].min, pages[0].cells[0].max, pages[0].cells[0].rest].join()');
    const warn = () => R('[pages[0].cells[0].has_warning, pages[0].cells[0].warning_threshold, pages[0].cells[0].warning_above].join()');
    R('setCellSignal(0, 0, 4)'); check(scale() === "4,10,20,10", "AFR: 10 to 20, rest at 10 (got " + scale() + ")");
    check(warn().startsWith("false"), "AFR has no template warning, so the warning is off (got " + warn() + ")");
    R('setCellSignal(0, 0, 2)'); check(scale() === "2,40,300,40" && warn() === "true,230,true", "coolant: 40 to 300, warn above 230 (got " + scale() + " / " + warn() + ")");
    R('setCellSignal(0, 0, 6)'); check(scale() === "6,40,300,40" && warn() === "true,260,true", "oil temperature: 40 to 300, warn above 260");
    R('setCellSignal(0, 0, 5)'); check(scale() === "5,0,100,0" && warn() === "true,10,false", "oil pressure: 0 to 100 psi, warn BELOW 10");
    R('setCellSignal(0, 0, 10)'); check(scale() === "10,8,16,8" && warn() === "true,12,false", "battery: 8 to 16 V, warn below 12");
    R('setCellSignal(0, 0, 1)'); check(scale() === "1,0,8000,0" && warn() === "true,7000,true", "RPM: 0 to 8000, warn above 7000");
    R('setCellSignal(0, 0, 3)'); check(scale() === "3,-15,30,0" && warn().startsWith("false"), "MAP: -15 to 30 psi boost, resting at 0 (atmosphere), and the RPM warning does NOT carry over");
    R('pages[0].cells[0].alert_fullscreen = true; setCellSignal(0, 0, 9)'); check(R('pages[0].cells[0].alert_fullscreen') === false, "switching to a signal with no warning also turns off the full-screen alert");
    R('pages[0].cells[0].min = 5; pages[0].cells[0].max = 50; pages[0].cells[0].rest = 7; pages[0].cells[0].has_warning = true;'); R('setCellSignal(0, 0, 14)');
    check(scale() === "14,5,50,7" && warn().startsWith("true"), "an ECU on/off flag (no template): the signal changes, everything you set is left alone");
    R('pages[0].cells[0].min = 5; pages[0].cells[0].max = 50; setCellSignal(0, 0, 4)'); check(scale() === "4,10,20,10", "a custom scale IS replaced when you pick a signal with a template");
    R('setCellSignal(0, 0, 4)'); check(E.els.pages && E.els.pages.innerHTML.includes('value="10"') && E.els.pages.innerHTML.includes('value="20"'), "the boxes on screen show the new Min and Max");

    console.log("2. the controls");
    const cell = o => R(`Object.assign({ enabled: true, signal: 2, gauge_type: 0, min: 0, max: 250, rest: 0, caption: "CLT", needle_color: 0xFFFFFF, tick_color: 0xFFFFFF, bg_color: 0, value_divisor: 1, tick_count: 26, major_nth: 5, refresh_hz: 10, show_value: false, value_font_size: 28, caption_font_size: 0, caption_on_face: false, units: "", v_align: 0, has_warning: false, warning_threshold: 0, warning_above: false }, ${JSON.stringify(o)})`);
    const html = o => { R(`globalThis.__c = ${JSON.stringify(cell(o))}`); return R('renderCell(0, 0, __c)'); };
    check(html({}).includes('<select onchange="setCellSignal(0,0,parseInt(this.value))">'), "the Signal dropdown on screen is wired to setCellSignal (so choosing AFR really sets 10-20)");
    for (const [gt, nm, hasPos, hasFace] of [[0, "needle", true, display], [1, "number", true, false], [2, "half moon", true, display], [3, "trend graph", false, false]]) {
      const h = html({ gauge_type: gt });
      check(h.includes("Caption size") && h.includes('maxlength="7"') && h.includes("Units"), `${nm}: caption size and units controls are shown`);
      check(h.includes("Gauge position in the cell") === hasPos, `${nm}: gauge position ${hasPos ? "shown" : "hidden (a trend graph fills its cell)"}`);
      check(h.includes("Show the caption on the dial face") === hasFace, `${nm}: 'caption on the dial face' ${hasFace ? "shown" : "not shown"}`);
    }
    { const h = html({ caption_font_size: 36 });
      const capSel = h.match(/Caption size <select[^>]*>([\s\S]*?)<\/select>/)[1];
      check((capSel.match(/<option /g) || []).length === 6 && capSel.includes('<option value="36" selected>36 Large</option>') && capSel.includes('>Normal</option>') && capSel.includes('>14 Small</option>') && capSel.includes('>48 XL</option>'), "caption size: Normal, 14, 20, 28, 36, 48 with the saved 36 selected");
      check(html({}).includes('<option value="0" selected>Normal</option>'), "an unset caption size shows as Normal");
      check(html({ v_align: 2 }).includes('<option value="2" selected>Bottom</option>') && html({ v_align: 1 }).includes('<option value="1" selected>Top</option>') && html({}).includes('<option value="0" selected>Center</option>'), "gauge position: Center / Top / Bottom with the saved one selected");
      { const ds = h => (h.match(/'decimals',parseInt\(this\.value\)\)">([\s\S]*?)<\/select>/) || [])[1] || "";
        check((ds(html({})).match(/<option /g) || []).length === 3 && ds(html({})).includes('<option value="1" selected>'), "decimals: 0 / 1 / 2, an unset one shows 1");
        check(ds(html({ decimals: 0 })).includes('<option value="0" selected>') && ds(html({ decimals: 2 })).includes('<option value="2" selected>') && ds(html({ decimals: 9 })).includes('<option value="1" selected>'), "decimals: the saved 0 / 2 is selected, an unknown value shows 1"); }
      const faceOn = /<input type="checkbox" checked onchange="setCellField\(0,0,'caption_on_face',this\.checked\)">/.test(html({ gauge_type: 0, caption_on_face: true }));
      const faceOff = /<input type="checkbox"  onchange="setCellField\(0,0,'caption_on_face',this\.checked\)">/.test(html({ gauge_type: 0, caption_on_face: false }));
      check(display ? (faceOn && faceOff) : (!faceOn && !faceOff), display ? "the face checkbox is ticked when saved on, and not ticked when off" : "the web dashboard editor has no face checkbox (there is no dial there)"); }
    { const h = html({ caption: 'a"><img src=x onerror=alert(1)>', units: '"><b>' });
      check(!h.includes('"><img') && !h.includes('"><b>') && h.includes("&quot;&gt;&lt;img") && h.includes("&quot;&gt;&lt;b&gt;"), "caption and units text are HTML-escaped in the editor"); }

    console.log("3. new cells and saving");
    const e = R('emptyCell()');
    check(e.caption_font_size === 0 && e.caption_on_face === false && e.units === "" && e.v_align === 0 && e.decimals === 1, "a new cell: Normal caption, not on the face, no units, centred, 1 decimal");
    R('setCellField(0, 0, "caption_font_size", 36); setCellField(0, 0, "units", "kPa"); setCellField(0, 0, "v_align", 2); setCellField(0, 0, "caption_on_face", true);');
    check(R('pages[0].cells[0].caption_font_size') === 36 && R('pages[0].cells[0].units') === "kPa" && R('pages[0].cells[0].v_align') === 2 && R('pages[0].cells[0].caption_on_face') === true, "the four settings are stored on the cell");
    R('save()'); await settle();
    const sent = E.posts.length ? JSON.parse(E.posts[E.posts.length - 1].body)[0].cells[0] : {};
    check(sent.caption_font_size === 36 && sent.units === "kPa" && sent.v_align === 2 && sent.caption_on_face === true, "all four are sent to the board when saving");
  }

  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})();
