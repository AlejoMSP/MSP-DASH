#ifndef WEB_THEME_H
#define WEB_THEME_H

/*
  web_theme.h
  The look shared by EVERY screen web page, the same as the hub's pages (one design for the whole site):
  black background, cyan headings, dark cards, cyan buttons, dark form fields, and a menu down the left side.
  Each page in web_monitor.cpp is built from these pieces plus its own content:

      PH_HEAD  "<title>..</title>"  PH_STYLE  <page's own <style>>  PH_OPEN
      <page content>  PH_CLOSE  <page's <script> and </body></html>>

  The compiler joins the pieces into one string, so there is no extra web request and the look can never differ from
  one page to the next. (tests/host/extract_page.js does the same joining so the tests see what the board serves.)

  Layout (hub 54 / build 217 on; before that it was an iPhone-shaped column with a bezel on desktops):
    - Wide window (800 px and up): the menu stays on the left, the page uses up to 1000 px beside it.
    - Narrow window or phone: the menu hides behind a button at the top left; the page uses the full width.
    - Inside the hub's Screens page (an iframe): no menu at all, the hub's menu is already there.
    - On iPhone: form fields are 16 px so Safari does not zoom in when one is tapped, and the notch / home bar are kept
      clear (safe-area insets).

  Written as ordinary string pieces (single quotes inside the HTML and CSS) because a raw string literal that spans
  several lines cannot be used inside a #define.
*/

#define PH_HEAD \
  "<!DOCTYPE html><html><head><meta charset='utf-8'>" \
  "<meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>" \
  "<meta name='theme-color' content='#000000'>"

#define PH_STYLE \
  "<style>" \
  "*{box-sizing:border-box}" \
  "html{-webkit-text-size-adjust:100%}" \
  "body{margin:0 auto;max-width:1000px;padding:12px 16px calc(24px + env(safe-area-inset-bottom));background:#000;color:#ddd;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif}" \
  "a{color:#4dd0e1}" \
  "h1{font-size:1.4em;color:#4dd0e1;margin:6px 0 12px}" \
  "h2{font-size:1.1em;color:#4dd0e1;margin:16px 0 8px}" \
  "button{padding:8px 14px;background:#4dd0e1;color:#000;border:none;border-radius:6px;font-size:15px;cursor:pointer}" \
  "button.danger{background:#F44336;color:#fff}" \
  "input,select,textarea{font-size:16px}" \
  "input:not([type=checkbox]):not([type=radio]):not([type=color]):not([type=range]):not([type=file]),select,textarea{background:#000;color:#ddd;border:1px solid #333;border-radius:4px;padding:6px}" \
  ".pad{padding:0}" \
  ".nav{position:fixed;left:0;top:0;bottom:0;width:200px;background:#0b1a1d;padding:12px 8px calc(12px + env(safe-area-inset-bottom));overflow-y:auto;z-index:50}" \
  ".nav .brand{display:block;color:#4dd0e1;font-weight:bold;font-size:18px;margin:2px 10px 2px;padding:0;text-decoration:none}" \
  ".nav .build{font-size:11px;color:#6b8a90;margin:0 10px 6px}.nav .build:empty{display:none}" \
  ".nav .grp{color:#6b8a90;font-size:11px;text-transform:uppercase;letter-spacing:.08em;margin:14px 10px 4px}" \
  ".nav a{display:block;color:#cfe8ec;text-decoration:none;padding:7px 10px;border-radius:6px;font-size:14px}" \
  ".nav a:hover{background:#13292d}.nav a.on{background:#1d3b40;color:#4dd0e1;font-weight:bold}" \
  ".navbtn{display:none;position:fixed;top:calc(8px + env(safe-area-inset-top));left:8px;z-index:60;background:#0b1a1d;color:#4dd0e1;border:1px solid #1d3b40;border-radius:6px;font-size:20px;padding:4px 10px}" \
  "@media(min-width:800px){body{margin-left:216px}}" \
  "@media(max-width:799px){.navbtn{display:block}.nav{transform:translateX(-100%);transition:transform .2s}.nav.open{transform:none;box-shadow:4px 0 20px #000}body{padding-top:calc(52px + env(safe-area-inset-top))}}" \
  "body.framed{margin:0!important;max-width:none;padding-top:12px!important}body.framed .nav,body.framed .navbtn{display:none}" \
  "</style>"

#define PH_OPEN \
  "</head><body><script data-theme>if(window.self!==window.top)document.body.className+=' framed'</script>" \
  "<button class='navbtn' onclick='document.querySelector(\".nav\").classList.toggle(\"open\")'>&#9776;</button>" \
  "<div class='nav'><a class='brand' href='/'>MSP-DASH</a><div class='build' id='build'></div>" \
    "<div class='grp'>This screen</div>" \
    "<a href='/'>Monitor</a>" \
    "<a href='/gauges'>Gauges</a>" \
    "<div class='grp'>Logging</div>" \
    "<a href='/log-channels'>Logging</a>" \
    "<a href='/sd'>SD card</a>" \
    "<div class='grp'>Setup</div>" \
    "<a href='/settings'>Board settings</a>" \
    "<a href='/update'>Firmware update</a>" \
  "</div>" \
  "<script data-theme>document.querySelectorAll('.nav a').forEach(function(a){if(a.className!=='brand'&&a.getAttribute('href')===location.pathname)a.className='on'})</script>" \
  "<div class='main'><div>"

// After the page content: puts this board's name next to MSP-DASH in the header and at the front of the browser tab
// title on EVERY page, so with three screens open you can tell which one you are on ("MSP-DASH Center",
// tab "Center | MSP-DASH Gauge Config"). Boards with no name keep plain "MSP-DASH".
#define PH_CLOSE "</div></div>" \
  "<script data-shared>try{fetch('/status.json').then(function(r){return r.json();}).then(function(st){" \
    "var n=st&&st.link&&st.link.name;if(!n)return;" \
    "var b=document.querySelector('.brand');if(b)b.textContent='MSP-DASH '+n;" \
    "if(document.title.indexOf(n+' | ')!==0)document.title=n+' | '+document.title;" \
  "}).catch(function(){});}catch(e){}</script>"

#endif
