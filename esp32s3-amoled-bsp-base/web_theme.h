#ifndef WEB_THEME_H
#define WEB_THEME_H

/*
  web_theme.h
  The look shared by EVERY web page: an iPhone-shaped column with a header and menu.
  Each page in web_monitor.cpp is built from these pieces plus its own content:

      PH_HEAD  "<title>..</title>"  PH_STYLE  <page's own <style>>  PH_OPEN
      <page content>  PH_CLOSE  <page's <script> and </body></html>>

  The compiler joins the pieces into one string, so there is no extra web request and
  the frame can never differ from one page to the next. (tests/host/extract_page.js
  does the same joining so the tests see exactly what the board serves.)

  What it does
    - The page is always laid out as an iPhone 15 Pro Max screen: a column at most 430 CSS
      pixels wide, centred.
    - On a DESKTOP (a mouse, and a window at least 500px wide) that column is drawn inside
      an iPhone bezel with a notch, like a preview.
    - On a real phone or tablet (touch only) there is NO bezel: the column simply fills the
      screen. This is decided by what the device can do (hover + fine pointer), NOT by window
      width, so an iPhone turned sideways does not suddenly get a frame.
    - On iPhone: content stays clear of the notch and home bar (safe-area insets), form
      fields are 16px so Safari does not zoom in when one is tapped, and "Add to Home Screen"
      opens it full-screen like an app.

  Written as ordinary string pieces (single quotes inside the HTML and CSS) because a raw
  string literal that spans several lines cannot be used inside a #define.
*/

#define PH_HEAD \
  "<!DOCTYPE html><html><head><meta charset='utf-8'>" \
  "<meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>" \
  "<meta name='apple-mobile-web-app-capable' content='yes'>" \
  "<meta name='mobile-web-app-capable' content='yes'>" \
  "<meta name='apple-mobile-web-app-title' content='MSP-DASH'>" \
  "<meta name='apple-mobile-web-app-status-bar-style' content='black-translucent'>" \
  "<meta name='theme-color' content='#000000'>"

#define PH_STYLE \
  "<style>" \
  "*{box-sizing:border-box}" \
  "html{-webkit-text-size-adjust:100%}" \
  "body{margin:0;background:#000;color:#fff;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif}" \
  "a{color:#4dd0e1}" \
  "h1{font-size:1.2em;text-align:center;margin:6px 0 12px}" \
  "button{padding:10px 14px;background:#4dd0e1;color:#000;border:none;border-radius:8px;font-size:15px}" \
  "button.danger{background:#F44336;color:#fff}" \
  "input,select,textarea{font-size:16px}" \
  ".phone-outer{width:100%;max-width:430px;margin:0 auto;min-height:100vh;min-height:100dvh;background:#000;position:relative}" \
  ".phone-screen{padding:env(safe-area-inset-top) env(safe-area-inset-right) env(safe-area-inset-bottom) env(safe-area-inset-left)}" \
  ".pad{padding:0 16px 24px}" \
  ".topbar{display:flex;align-items:center;justify-content:space-between;padding:16px}" \
  ".brand{display:block;color:#fff;font-weight:bold;font-size:1.1em;text-decoration:none}" \
  ".build{font-size:.68em;color:#777;margin-top:2px}" \
  ".build:empty{display:none}" \
  ".hamburger{background:none;border:1px solid #444;color:#fff;font-size:1.3em;padding:2px 10px;border-radius:6px}" \
  ".navmenu{display:none;flex-direction:column;background:#111;border-radius:8px;padding:6px 10px;margin:0 16px 12px}" \
  ".navmenu.open{display:flex}" \
  ".navmenu a{color:#4dd0e1;padding:10px 6px;text-decoration:none}" \
  ".navmenu a+a{border-top:1px solid #222}" \
  "@media (hover:hover) and (pointer:fine) and (min-width:500px){" \
    "body{background:#222;padding:40px 0}" \
    ".phone-outer{min-height:932px;border-radius:48px;border:10px solid #111;box-shadow:0 20px 60px rgba(0,0,0,.6);overflow:hidden}" \
    ".phone-outer:before{content:'';position:absolute;top:14px;left:50%;transform:translateX(-50%);width:120px;height:28px;background:#111;border-radius:16px;z-index:10}" \
    ".phone-screen{padding-top:46px}" \
  "}" \
  "</style>"

#define PH_OPEN \
  "</head><body><div class='phone-outer'><div class='phone-screen'>" \
  "<div class='topbar'><div class='titleblock'><a class='brand' href='/'>MSP-DASH</a><div class='build' id='build'></div></div>" \
  "<button class='hamburger' onclick='document.getElementById(\"navmenu\").classList.toggle(\"open\")'>&#9776;</button></div>" \
  "<div class='navmenu' id='navmenu'>" \
    "<a href='/'>Monitor</a>" \
    "<a href='/gauges'>Display Gauge Config</a>" \
    "<a href='/log-channels'>Logging</a>" \
    "<a href='/sd'>SD Card</a>" \
    "<a href='/settings'>Board Settings</a>" \
    "<a href='/update'>Firmware Update</a>" \
  "</div>"

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
