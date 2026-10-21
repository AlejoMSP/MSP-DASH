/*
  web_indicator_editor.h
  The INDICATORS page editor: lamp cards, presets, preview chips, add / remove / reorder. It is ONE raw string
  literal that web_monitor.cpp #includes into the script of BOTH the display editor (/gauges) and the web dashboard
  editor (/web-config), so the two can never differ. It relies on the editors' own `pages` array, `signalActivity`
  (from /signals.json) and renderPages().
*/
R"rawliteral(
    // =====================================================================================
    // Indicators page editor (shared by the display editor and the web dashboard editor)
    // =====================================================================================
    const IND_MAX = 12;
    // Signal numbers = the order of SignalId in gauge_config.h (tests/host/test_editor_indicators.js checks this table against that file).
    const SIG = { RPM:1, CLT:2, MAP:3, AFR:4, OILP:5, OILT:6, FUELP:7, FLEX:8, TPS:9, BATTV:10,
                  REVLIM:11, MAINRELAY:12, FUELPUMP:13, CEL:14, EGOHEAT:15, LAMBDAPROT:16, FAN:17, FAN2:18, BRAKE:19, EGT:20, IAT:21, AMBT:22, HUM:23, FUEL:24, VSS:25 };
    const SIGNAL_UNITS = { 1:"rpm", 2:"F", 3:"psi", 4:"AFR", 5:"psi", 6:"F", 7:"psi", 8:"%", 9:"%", 10:"V", 20:"F", 21:"F", 22:"F", 23:"%", 24:"%", 25:"mph" };
    const IND_RED = 0xE53935, IND_AMBER = 0xFFB300, IND_GREEN = 0x43A047, IND_BLUE = 0x1E88E5, IND_OFF = 0x2A2A2A;

    // Ready-made lamps. A threshold lamp is "above" (op 0) or "below" (op 1) a value, in the signal's own units; an ECU on/off
    // flag is 1 or 0, so "flag on" is above 0.5. hyst = hysteresis: once on, the lamp stays on until the value is that far back.
    const IND_PRESETS = [
      { name:"Low oil pressure",   signal:SIG.OILP,  op:1, threshold:10,   hyst:2,   text:"OIL",   color:IND_RED },
      { name:"High coolant temp",  signal:SIG.CLT,   op:0, threshold:220,  hyst:5,   text:"TEMP",  color:IND_RED },
      { name:"High oil temp",      signal:SIG.OILT,  op:0, threshold:240,  hyst:5,   text:"OIL T", color:IND_RED },
      { name:"Low battery",        signal:SIG.BATTV, op:1, threshold:12,   hyst:0.3, text:"BATT",  color:IND_AMBER },
      { name:"Lean mixture",       signal:SIG.AFR,   op:0, threshold:15.5, hyst:0.3, text:"LEAN",  color:IND_AMBER },
      { name:"Low fuel pressure",  signal:SIG.FUELP, op:1, threshold:30,   hyst:2,   text:"FUEL",  color:IND_RED },
      { name:"Boost",              signal:SIG.MAP,   op:0, threshold:0.5,  hyst:0.3,   text:"BOOST", color:IND_BLUE },
      { name:"High ethanol (E85)", signal:SIG.FLEX,  op:0, threshold:50,   hyst:2,   text:"E85",   color:IND_GREEN },
      { name:"Rev limit (by RPM)", signal:SIG.RPM,   op:0, threshold:6800, hyst:100, text:"REV",   color:IND_RED, blink:true },
      { name:"Check engine light", signal:SIG.CEL,        op:0, threshold:0.5, hyst:0, text:"CHECK ENG", color:IND_AMBER },
      { name:"Rev limiter active", signal:SIG.REVLIM,     op:0, threshold:0.5, hyst:0, text:"REV LIM",   color:IND_RED, blink:true },
      { name:"Fan 1",              signal:SIG.FAN,        op:0, threshold:0.5, hyst:0, text:"FAN 1",     color:IND_BLUE },
      { name:"Fan 2",              signal:SIG.FAN2,       op:0, threshold:0.5, hyst:0, text:"FAN 2",     color:IND_BLUE },
      { name:"Fuel pump",          signal:SIG.FUELPUMP,   op:0, threshold:0.5, hyst:0, text:"FUEL PUMP", color:IND_GREEN },
      { name:"Main relay",         signal:SIG.MAINRELAY,  op:0, threshold:0.5, hyst:0, text:"RELAY",     color:IND_GREEN },
      { name:"O2 heater",          signal:SIG.EGOHEAT,    op:0, threshold:0.5, hyst:0, text:"O2 HEAT",   color:IND_AMBER },
      { name:"Lambda protection",  signal:SIG.LAMBDAPROT, op:0, threshold:0.5, hyst:0, text:"LAMBDA",    color:IND_AMBER },
      { name:"Brake pedal",        signal:SIG.BRAKE,      op:0, threshold:0.5, hyst:0, text:"BRAKE",     color:IND_RED },
    ];
    // What "+ Add Indicators Page" starts with (by preset name); the user then changes whatever they like.
    const IND_DEFAULT_PAGE = ["Check engine light", "Low oil pressure", "High coolant temp", "Low battery", "Rev limiter active", "Fan 1"];

    // The lamp a preset makes. The off look shows the same text, dim on dark grey, like a labelled warning-light panel.
    function presetLamp(p) {
      return { signal: p.signal, op: p.op, threshold: p.threshold, hysteresis: p.hyst || 0,
               on_text: p.text, off_text: p.text, on_color: p.color, off_color: IND_OFF,
               on_text_color: p.color === IND_AMBER ? 0x000000 : 0xFFFFFF, off_text_color: 0x808080, blink: !!p.blink };
    }
    function blankLamp() {
      return { signal: 0, op: 0, threshold: 0, hysteresis: 0, on_text: "", off_text: "", on_color: IND_RED, off_color: IND_OFF,
               on_text_color: 0xFFFFFF, off_text_color: 0x808080, blink: false };
    }

    const hex6 = n => "#" + ((Number(n) >>> 0) & 0xFFFFFF).toString(16).padStart(6, "0");
    const escHtml = s => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
    const signalInfo = id => signalActivity.find(s => s.id === id);

    // Every signal: the analog ones first, then the ECU's on/off flags. Units are shown so thresholds are typed in the right ones.
    function indicatorSignalOptions(selected) {
      const opt = s => {
        const unit = SIGNAL_UNITS[s.id];
        const liveTag = s.age_ms < 5000 ? " (live)" : "";
        return `<option value="${s.id}" ${selected === s.id ? "selected" : ""}>${escHtml(s.name)}${unit ? " [" + unit + "]" : ""}${liveTag}</option>`;
      };
      const byName = (a, b) => a.name.localeCompare(b.name);   // alphabetical, like the gauge editor's list
      const analog = signalActivity.filter(s => s.id !== 0 && !s.flag).sort(byName);
      const flags = signalActivity.filter(s => s.flag).sort(byName);
      return `<option value="0" ${selected === 0 ? "selected" : ""}>None</option>` + analog.map(opt).join("") +
             (flags.length ? `<optgroup label="ECU on/off flags">${flags.map(opt).join("")}</optgroup>` : "");
    }

    // The two small previews on a lamp card: how it looks ON and how it looks OFF.
    function lampChip(l, on) {
      const text = on ? l.on_text : l.off_text;
      return `<span class="chip" style="background:${hex6(on ? l.on_color : l.off_color)};color:${hex6(on ? l.on_text_color : l.off_text_color)}">${escHtml(text) || "&nbsp;"}</span>`;
    }
    function refreshChips(pIdx, i) {
      const el = document.getElementById("chips_" + pIdx + "_" + i);
      if (el) el.innerHTML = lampChip(pages[pIdx].indicators[i], true) + lampChip(pages[pIdx].indicators[i], false);
    }

    function renderLamp(pIdx, i, l, count) {
      const info = signalInfo(l.signal), isFlag = !!(info && info.flag), unit = SIGNAL_UNITS[l.signal] || "";
      const id = pIdx + "," + i;
      return `
        <div class="cell lamp-card">
          <div class="lamp-head">
            <strong>Indicator ${i + 1}</strong>
            <span class="chips" id="chips_${pIdx}_${i}">${lampChip(l, true)}${lampChip(l, false)}</span>
            <span class="lamp-btns">
              <button type="button" onclick="moveLamp(${id},-1)" ${i === 0 ? "disabled" : ""} title="Move up">&uarr;</button>
              <button type="button" onclick="moveLamp(${id},1)" ${i === count - 1 ? "disabled" : ""} title="Move down">&darr;</button>
              <button type="button" class="danger" onclick="removeLamp(${id})" title="Remove this indicator">&times;</button>
            </span>
          </div>
          <label>Start from a preset
            <select onchange="applyPreset(${id}, this.value)"><option value="">Choose...</option>${IND_PRESETS.map((p, k) => `<option value="${k}">${escHtml(p.name)}</option>`).join("")}</select>
          </label>
          <label>Signal <select onchange="setLampSignal(${id}, parseInt(this.value))">${indicatorSignalOptions(l.signal)}</select></label>
          ${isFlag ? `
          <label>Lamp is on while the ECU flag is
            <select onchange="setFlagMode(${id}, this.value === '1')">
              <option value="1" ${l.op === 0 ? "selected" : ""}>On</option>
              <option value="0" ${l.op === 1 ? "selected" : ""}>Off</option>
            </select>
          </label>` : `
          <div class="row2">
            <div><label>On when value is
              <select onchange="setLampField(${id},'op',parseInt(this.value))">
                <option value="0" ${l.op === 0 ? "selected" : ""}>Above</option>
                <option value="1" ${l.op === 1 ? "selected" : ""}>Below</option>
              </select></label></div>
            <div><label>Threshold${unit ? " (" + unit + ")" : ""} <input type="number" step="any" value="${l.threshold}" oninput="setLampNumber(${id},'threshold',this.value)"></label></div>
            <div><label>Hysteresis <input type="number" step="any" min="0" value="${l.hysteresis}" oninput="setLampNumber(${id},'hysteresis',this.value)"></label></div>
          </div>
          <p class="hint">Hysteresis keeps a lamp from flickering: once on it stays on until the value is that far back past the threshold. A lamp is always off while its signal has no data.</p>`}
          <div class="row2">
            <div><label>On text <input type="text" maxlength="11" value="${escHtml(l.on_text)}" oninput="setLampText(${id},'on_text',this.value)"></label></div>
            <div><label>Off text <input type="text" maxlength="11" value="${escHtml(l.off_text)}" oninput="setLampText(${id},'off_text',this.value)"></label></div>
          </div>
          <div class="row2">
            <div><label>On color <input type="color" value="${hex6(l.on_color)}" oninput="setLampColor(${id},'on_color',this.value)"></label></div>
            <div><label>On text color <input type="color" value="${hex6(l.on_text_color)}" oninput="setLampColor(${id},'on_text_color',this.value)"></label></div>
          </div>
          <div class="row2">
            <div><label>Off color <input type="color" value="${hex6(l.off_color)}" oninput="setLampColor(${id},'off_color',this.value)"></label></div>
            <div><label>Off text color <input type="color" value="${hex6(l.off_text_color)}" oninput="setLampColor(${id},'off_text_color',this.value)"></label></div>
          </div>
          <label><input type="checkbox" ${l.blink ? "checked" : ""} onchange="setLampField(${id},'blink',this.checked)"> Flash while on</label>
        </div>`;
    }

    function renderIndicatorPage(pIdx, p) {
      const lamps = p.indicators || (p.indicators = []);
      const cols = Math.max(1, Math.min(4, p.cols || 2));
      return `
        <div class="page-card">
          <div class="page-header">
            <strong>Page ${pIdx + 1}</strong> <span class="badge">Indicators</span>
            <select onchange="setIndCols(${pIdx}, parseInt(this.value))" title="Indicators per row">
              ${[1, 2, 3, 4].map(n => `<option value="${n}" ${cols === n ? "selected" : ""}>${n} column${n > 1 ? "s" : ""}</option>`).join("")}
            </select>
            <button type="button" title="Move up" onclick="pageAction(${pIdx},'up')" ${pIdx === 0 ? "disabled" : ""}>&uarr;</button>
            <button type="button" title="Move down" onclick="pageAction(${pIdx},'down')" ${pIdx === pages.length - 1 ? "disabled" : ""}>&darr;</button>
            <button type="button" onclick="pageAction(${pIdx},'copy')">Copy</button>
            <button type="button" onclick="pageAction(${pIdx},'empty')">Empty</button>
            <button type="button" class="danger" onclick="pageAction(${pIdx},'delete')">Delete</button>
          </div>
          ${lamps.map((l, i) => renderLamp(pIdx, i, l, lamps.length)).join("")}
          <button type="button" class="addlamp" onclick="addLamp(${pIdx})" ${lamps.length >= IND_MAX ? "disabled" : ""}>+ Add indicator${lamps.length >= IND_MAX ? " (12 is the most)" : ""}</button>
        </div>`;
    }

    function addIndicatorPage() {
      pages.push({ enabled: true, kind: 1, cols: 2,
                   indicators: IND_DEFAULT_PAGE.map(name => presetLamp(IND_PRESETS.find(p => p.name === name))) });
      renderPages();
    }
    function addLamp(pIdx) {
      const lamps = pages[pIdx].indicators;
      if (lamps.length >= IND_MAX) return;
      lamps.push(blankLamp());
      renderPages();
    }
    function removeLamp(pIdx, i) { pages[pIdx].indicators.splice(i, 1); renderPages(); }
    function moveLamp(pIdx, i, delta) {
      const lamps = pages[pIdx].indicators, j = i + delta;
      if (j < 0 || j >= lamps.length) return;
      const t = lamps[i]; lamps[i] = lamps[j]; lamps[j] = t;
      renderPages();
    }
    function setIndCols(pIdx, cols) { pages[pIdx].cols = Math.max(1, Math.min(4, cols || 2)); }
    function applyPreset(pIdx, i, k) {
      if (k === "" || IND_PRESETS[parseInt(k)] === undefined) return;
      Object.assign(pages[pIdx].indicators[i], presetLamp(IND_PRESETS[parseInt(k)]));
      renderPages();
    }
    // Choosing an ECU flag sets "on while the flag is on" (above 0.5); choosing an analog signal starts with a clean threshold.
    function setLampSignal(pIdx, i, sig) {
      const l = pages[pIdx].indicators[i], info = signalInfo(sig);
      l.signal = sig;
      if (info && info.flag) { l.op = 0; l.threshold = 0.5; l.hysteresis = 0; }
      else if (l.threshold === 0.5 && l.hysteresis === 0) { l.threshold = 0; }
      renderPages();
    }
    function setFlagMode(pIdx, i, flagOn) {
      const l = pages[pIdx].indicators[i];
      l.op = flagOn ? 0 : 1; l.threshold = 0.5; l.hysteresis = 0;
    }
    function setLampField(pIdx, i, key, value) { pages[pIdx].indicators[i][key] = value; }
    function setLampNumber(pIdx, i, key, value) {
      const n = parseFloat(value);
      if (isNaN(n)) return;
      pages[pIdx].indicators[i][key] = key === "hysteresis" ? Math.max(0, n) : n;
    }
    function setLampText(pIdx, i, key, value) { pages[pIdx].indicators[i][key] = value; refreshChips(pIdx, i); }
    function setLampColor(pIdx, i, key, hex) { pages[pIdx].indicators[i][key] = parseInt(hex.substring(1), 16); refreshChips(pIdx, i); }

    // =====================================================================================
    // Settings every gauge cell has, shared by both editors
    // =====================================================================================
    // Gauge templates: choosing a signal fills in its usual scale and warning. The same list as GAUGE_DEFAULT_RANGES in
    // gauge_style.h (tests/host/test_editor_cells.js reads that file and checks they agree).
    // [min, max, warning on (1/0), warning level, 1 = warn above / 0 = warn below]. Temperatures F, pressures psi, MAP is boost psi.
    const SIGNAL_DEFAULT_RANGES = {
      [SIG.RPM]: [0, 8000, 1, 7000, 1], [SIG.CLT]: [40, 300, 1, 230, 1], [SIG.MAP]: [-15, 30, 0, 0, 1], [SIG.AFR]: [10, 20, 0, 0, 1],
      [SIG.OILP]: [0, 100, 1, 10, 0], [SIG.OILT]: [40, 300, 1, 260, 1], [SIG.FUELP]: [0, 100, 0, 0, 0], [SIG.FLEX]: [0, 100, 0, 0, 1],
      [SIG.TPS]: [0, 100, 0, 0, 1], [SIG.BATTV]: [8, 16, 1, 12, 0], [SIG.EGT]: [0, 1800, 1, 1600, 1], [SIG.IAT]: [0, 200, 1, 150, 1],
      [SIG.AMBT]: [0, 120, 0, 0, 1], [SIG.HUM]: [0, 100, 0, 0, 1], [SIG.FUEL]: [0, 100, 1, 15, 0], [SIG.VSS]: [0, 160, 0, 0, 1] };
    // Caption / units text sizes. 0 = "Normal": the display's default font, as captions always looked. A size too big for the cell shrinks to the largest that fits.
    const CAPTION_SIZES = [[0, "Normal"], [14, "14 Small"], [20, "20"], [28, "28 Medium"], [36, "36 Large"], [48, "48 XL"]];
    const VALIGNS = [[0, "Center"], [1, "Top"], [2, "Bottom"]];
    const DECIMALS = [[0, "0  (14)"], [1, "1  (14.7)"], [2, "2  (14.70)"]];
    // Digits after the point for a cell's readout; anything unset or unknown is 1, what readouts always showed.
    function decimalsOf(c) { const d = Number(c && c.decimals); return (d === 0 || d === 1 || d === 2) ? d : 1; }

    // Choosing a signal starts the gauge from its template: scale, needle rest at the bottom (MAP at 0, zero boost), and the warning (turned
    // off when the template has none, so a warning left from the old signal does not carry over). Signals without a
    // template (the ECU on/off flags) keep whatever the cell has.
    function setCellSignal(pIdx, cIdx, sig) {
      const c = pages[pIdx].cells[cIdx];
      c.signal = sig;
      const r = SIGNAL_DEFAULT_RANGES[sig];
      if (!r) return;
      c.min = r[0]; c.max = r[1]; c.rest = sig === SIG.MAP ? 0 : r[0];   // MAP rests at 0: the atmosphere, zero boost
      c.has_warning = !!r[2];
      if (r[2]) { c.warning_threshold = r[3]; c.warning_above = !!r[4]; }
      else c.alert_fullscreen = false;
      renderPages();
    }
    function setCellField(pIdx, cIdx, key, value) { pages[pIdx].cells[cIdx][key] = value; }
    function captionSizeOptions(selected) {
      return CAPTION_SIZES.map(([v, n]) => `<option value="${v}" ${(selected || 0) === v ? "selected" : ""}>${n}</option>`).join("");
    }
    function decimalsOptions(selected) {
      const d = decimalsOf({ decimals: selected });
      return DECIMALS.map(([v, n]) => `<option value="${v}" ${d === v ? "selected" : ""}>${n}</option>`).join("");
    }
    function valignOptions(selected) {
      return VALIGNS.map(([v, n]) => `<option value="${v}" ${(selected || 0) === v ? "selected" : ""}>${n}</option>`).join("");
    }

    (function addIndicatorStyles() {
      if (!document.createElement || !document.head) return;
      const st = document.createElement("style");
      st.textContent = ".badge{background:#4dd0e1;color:#000;border-radius:10px;padding:1px 8px;font-size:0.7em;font-weight:bold}" +
        ".lamp-head{display:flex;align-items:center;gap:6px;flex-wrap:wrap;margin-bottom:6px}.lamp-btns{margin-left:auto;display:flex;gap:4px}" +
        ".lamp-btns button{padding:4px 10px;font-size:14px}.lamp-btns button:disabled,.addlamp:disabled{opacity:.35}" +
        ".chips{display:flex;gap:4px}.chip{border-radius:6px;padding:2px 8px;font-size:0.72em;font-weight:bold;border:1px solid rgba(255,255,255,.25);min-width:2.5em;text-align:center}" +
        ".addlamp{width:100%;margin:6px 0 0;background:none;border:1px dashed #4dd0e1;color:#4dd0e1}";
      document.head.appendChild(st);
    })();
)rawliteral"
