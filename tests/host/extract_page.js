// Rebuilds a web page exactly as the board serves it. Pages in web_monitor.cpp are C++ expressions made of raw
// string literals, ordinary string literals and macros (the shared frame in web_theme.h), joined by the compiler.
// This does the same joining so tests and the mock board see the real page.
//   node extract_page.js INDEX_HTML > index.html
const fs = require('fs');
const path = require('path');
const SKETCH = process.env.SKETCH_DIR || path.join(__dirname, '..', '..', 'esp32s3-amoled-bsp-base');   // SKETCH_DIR: test a modified copy

function tokenize(src) {            // -> [{t:'str', v}, {t:'id', v}, {t:'nl'}]  (comments and whitespace dropped)
  const out = []; let i = 0; const n = src.length;
  while (i < n) {
    const c = src[i];
    if (c === '\\' && src[i + 1] === '\n') { i += 2; continue; }                    // line continuation
    if (c === '\n') { out.push({ t: 'nl' }); i++; continue; }
    if (/\s/.test(c)) { i++; continue; }
    if (src.startsWith('//', i)) { while (i < n && src[i] !== '\n') i++; continue; }
    if (src.startsWith('/*', i)) { i = src.indexOf('*/', i) + 2; continue; }
    if (c === 'R' && src[i + 1] === '"') {                                            // raw string literal
      const open = src.indexOf('(', i); const delim = src.slice(i + 2, open);
      const close = src.indexOf(')' + delim + '"', open);
      out.push({ t: 'str', v: src.slice(open + 1, close) }); i = close + delim.length + 2; continue;
    }
    if (c === '"') {                                                                  // ordinary string literal
      let j = i + 1, v = '';
      while (src[j] !== '"') {
        if (src[j] === '\\') { const e = src[j + 1]; v += ({ n: '\n', t: '\t', r: '\r', '0': '\0' })[e] ?? e; j += 2; }
        else if (src[j] === '\n') throw new Error('newline inside a string literal');
        else v += src[j++];
      }
      out.push({ t: 'str', v }); i = j + 1; continue;
    }
    if (/[A-Za-z_]/.test(c)) { let j = i; while (j < n && /\w/.test(src[j])) j++; out.push({ t: 'id', v: src.slice(i, j) }); i = j; continue; }
    out.push({ t: 'other', v: c }); i++;
  }
  return out;
}

function loadMacros() {
  const macros = {};
  for (const f of fs.readdirSync(SKETCH).filter(f => f.endsWith('.h') || f.endsWith('.cpp'))) {
    const text = fs.readFileSync(path.join(SKETCH, f), 'utf8');
    const re = /^[ \t]*#define[ \t]+(PH_\w+)[ \t]*((?:[^\n\\]|\\\n|\\(?!\n))*)/gm;   // up to the first newline that is not escaped
    let m; while ((m = re.exec(text))) macros[m[1]] = tokenize(m[2]);
  }
  return macros;
}

function pageHtml(name) {
  const macros = loadMacros();
  const cpp = fs.readFileSync(path.join(SKETCH, 'web_monitor.cpp'), 'utf8');
  const start = cpp.indexOf('static const char ' + name + '[] PROGMEM =');
  if (start < 0) throw new Error('no such page: ' + name);
  const exprStart = cpp.indexOf('=', start) + 1;
  // the expression ends at the first ';' that is outside every literal
  let end = exprStart, toks = [];
  // `#include "file.h"` lines inside a page expression (e.g. the indicators editor shared by two pages) are replaced by that
  // file's text, the way the compiler does it, before the expression is read.
  const rest = cpp.slice(exprStart).replace(/^[ \t]*#include[ \t]+"([^"]+)"[ \t]*$/gm, (line, file) => {
    const f = path.join(SKETCH, file);
    return fs.existsSync(f) ? fs.readFileSync(f, 'utf8') : line;
  });
  const all = tokenize(rest);
  for (const t of all) { if (t.t === 'other' && t.v === ';') break; toks.push(t); }
  const expand = (list, depth = 0) => {
    if (depth > 10) throw new Error('macro loop');
    let s = '';
    for (const t of list) {
      if (t.t === 'str') s += t.v;
      else if (t.t === 'id') { if (!macros[t.v]) throw new Error('unknown identifier in page ' + name + ': ' + t.v); s += expand(macros[t.v], depth + 1); }
      else if (t.t === 'nl') continue;
      else throw new Error('unexpected token in page ' + name + ': ' + JSON.stringify(t));
    }
    return s;
  };
  return expand(toks);
}

function pageScript(name) { const m = pageHtml(name).match(/<script>([\s\S]*?)<\/script>/); return m ? m[1] : null; }
function pageNames() { return [...fs.readFileSync(path.join(SKETCH, 'web_monitor.cpp'), 'utf8').matchAll(/static const char (\w+_HTML)\[\] PROGMEM =/g)].map(m => m[1]); }

module.exports = { pageHtml, pageScript, pageNames };
if (require.main === module) process.stdout.write(pageHtml(process.argv[2]));
