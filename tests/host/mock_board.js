// A stand-in for the board's web server, for looking at the pages in a real browser on a PC.
//   node mock_board.js [port]      then open http://localhost:8080/
// Serves the real pages (assembled by extract_page.js) plus believable data. Needs the 'ws' package.
const http = require('http');
const { pageHtml } = require('./extract_page.js');
let WebSocketServer; try { ({ WebSocketServer } = require('ws')); } catch (e) { console.error("npm install ws  (needed for the live connection)"); process.exit(1); }

const PAGES = { '/': 'INDEX_HTML', '/can': 'CAN_HTML', '/gauges': 'GAUGES_HTML', '/web-config': 'WEBCFG_HTML', '/sd': 'SD_HTML', '/update': 'OTA_HTML', '/log-channels': 'LOGCHANS_HTML', '/simulator': 'SIM_HTML' };
const SIGNALS = ['None', 'RPM', 'Coolant Temp', 'MAP/Boost', 'AFR', 'Oil Pressure', 'Oil Temp', 'Fuel Pressure', 'Ethanol %', 'Throttle Position', 'Battery Voltage'];
const cell = (o) => Object.assign({ enabled: true, signal: 1, gauge_type: 0, min: 0, max: 100, rest: 0, caption: 'RPM', needle_color: 0xFFFFFF, tick_color: 0xFFFFFF, bg_color: 0, value_divisor: 1, tick_count: 26, major_nth: 5, refresh_hz: 10, show_value: false, value_font_size: 28, has_warning: false, warning_threshold: 0, warning_above: false, alert_fullscreen: false, history_sec: 30, ref_enabled: false, ref_value: 0 }, o);
const webPages = [
  { enabled: true, layout: 3, cells: [cell({ signal: 1, caption: 'RPM' }), cell({ signal: 3, caption: 'MAP' }), cell({ signal: 2, caption: 'CLT', has_warning: true, warning_threshold: 220, warning_above: true }), cell({ signal: 4, caption: 'AFR' })] },
  { enabled: true, layout: 2, cells: [cell({ signal: 5, caption: 'OIL PRESS' }), cell({ signal: 6, caption: 'OIL TEMP' }), cell({ signal: 7, caption: 'FUEL PRESS' })] }];
const gaugePages = ['CLT', 'MAP', 'AFR', 'OIL PRESS', 'OIL TEMP', 'FUEL PRESS', 'ETH %'].map((c, i) => ({ enabled: true, layout: 0, cells: [cell({ signal: [2, 3, 4, 5, 6, 7, 8][i], caption: c, max: 250, has_warning: i === 0 || i === 3, warning_threshold: 40 })] }));
gaugePages.push({ enabled: true, layout: 3, cells: [cell({ signal: 1, caption: 'RPM', gauge_type: 3, history_sec: 30, ref_enabled: true, ref_value: 3000 }), cell({ caption: 'MAP', signal: 3 }), cell({ caption: 'AFR', signal: 4, gauge_type: 1 }), cell({ caption: 'CLT', signal: 2 })] });

const JSON_ROUTES = {
  '/web-pages.json': () => webPages,
  '/gauges.json': () => gaugePages,
  '/signals.json': () => SIGNALS.map((n, i) => ({ id: i, name: n, age_ms: i === 0 ? 4294967295 : i * 40 })),
  '/status.json': () => ({ build: 104, compiled: 'Oct  7 2026 06:43:44', uptime_s: 83, can: { frames_per_s: 412, ids_seen: 38, state: 'running' }, storage: { mounted: true } }),
  '/can.json': () => [[0x201, 8, '5A 0B 00 00 00 00 00 1E', 20, 12], [0x202, 8, '00 00 E8 03 00 00 00 00', 20, 31], [0x203, 8, 'F4 0A 4B 00 52 00 00 00', 20, 8], [0x204, 8, '00 00 B4 00 00 00 36 35', 20, 44], [0x207, 8, 'FE 5C 00 00 3B 06 00 00', 20, 17], [0x190, 8, '12 34 56 78 9A BC DE F0', 99, 3], [0xEF50000, 8, '00 00 00 00 00 00 00 00', 10, 91]]
    .map(([id, dlc, data, hz, ago]) => ({ id, dlc, data, hz, ago })),
  '/sd/list': () => [{ name: 'log_16.csv', size: 48213 }, { name: 'log_17.csv', size: 1530922 }, { name: 'a_rather_long_file_name_for_testing_overflow.csv', size: 12 }],
  '/log-channels.json': () => SIGNALS.slice(1).map((n, i) => ({ id: i + 1, name: n, enabled: i % 3 !== 2 })),
  '/sim.json': () => ({ active: true, manual: true, seconds_left: 543, stop_reason: '', signals: SIGNALS.slice(1).map((n, i) => ({ id: i + 1, name: n, min: 0, max: 9000, step: i === 3 ? 0.1 : 1, value: 100 * (i + 1) })) }),
};

function makeServer() {
  const server = http.createServer((req, res) => {
    const url = req.url.split('?')[0];
    if (req.method === 'POST') { req.resume(); req.on('end', () => { res.writeHead(200, { 'Content-Type': 'text/plain' }); res.end('OK'); }); return; }
    if (PAGES[url]) { res.writeHead(200, { 'Content-Type': 'text/html' }); res.end(pageHtml(PAGES[url])); return; }
    if (JSON_ROUTES[url]) { res.writeHead(200, { 'Content-Type': 'application/json' }); res.end(JSON.stringify(JSON_ROUTES[url]())); return; }
    res.writeHead(404); res.end('Not found');
  });
  const wss = new WebSocketServer({ server, path: '/ws' });
  setInterval(() => {
    const t = Date.now() / 1000;
    const msg = JSON.stringify({ v: { 1: Math.round(3000 + 2000 * Math.sin(t / 3)), 2: 188.2, 3: 112.4, 4: 14.7, 5: 62, 6: 201.5, 7: 58, 8: 31 }, fps: 412, ids: 38, sim: 0 });
    wss.clients.forEach(c => c.readyState === 1 && c.send(msg));
  }, 500);
  return server;
}
module.exports = { makeServer };
if (require.main === module) { const port = Number(process.argv[2]) || 8080; makeServer().listen(port, () => console.log('mock board on http://localhost:' + port + '/')); }
