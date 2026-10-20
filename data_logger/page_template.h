#pragma once

// The logger's web page, served as-is by handleRoot(). Its JavaScript (bottom
// of this file) asks the board for /data every 2 seconds. When the page opens
// it also sends the phone's date and time to /settime, which is how the board
// learns what time it is. The colours and layout come from /style.css (see
// eltech_wifi.h).
const char PAGE_TEMPLATE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ElTech-Online Data Logger</title>
  <link rel="stylesheet" href="/style.css">
</head>
<body>
  <h1>ElTech-Online</h1>
  <div class="sub">ESP32 Data Logger &mdash; live readings, updated every 2 seconds</div>
  <div class="cards">
    <div class="card"><div class="label">Temperature</div><div class="value" id="t">--</div></div>
    <div class="card"><div class="label">Humidity</div><div class="value" id="h">--</div></div>
    <div class="card"><div class="label">Pressure</div><div class="value" id="p">--</div></div>
  </div>
  <div class="panel">
    <div class="head"><h2>Log file on the SD card</h2><span class="badge" id="card">--</span></div>
    <div class="row"><span class="name">Card</span><span id="detail">--</span></div>
    <div class="row"><span class="name">Rows written since power-on</span><span id="rows">--</span></div>
    <div class="row"><span class="name">File size</span><span id="size">--</span></div>
    <div class="row"><span class="name">Last row</span><span class="detail" id="last">--</span></div>
    <div class="row"><span class="name">Write a row every (seconds)</span>
      <input type="number" id="interval" min="2" max="3600" onchange="post('/interval', 's=' + this.value)"></div>
    <div class="buttons">
      <button onclick="location.href='/download'">Download log.csv</button>
      <button class="quiet" onclick="if (confirm('Delete the log file?')) post('/clear', '')">Delete the file</button>
    </div>
  </div>
  <div class="panel">
    <div class="head"><h2>Clock</h2><span class="badge" id="clockstate">--</span></div>
    <div class="row"><span class="name">Board time</span><span id="clock">--</span></div>
    <div class="msg">The board has no battery clock. It takes the time from this device whenever the page is opened.</div>
  </div>
  <div class="footer">ESP32 + AHT20 + BMP280 + microSD &middot; <a href="https://github.com/eltech-online/eltech-esp32-data-logger" target="_blank">github.com/eltech-online/eltech-esp32-data-logger</a></div>
  <script>
    const el = (id) => document.getElementById(id);
    // Puts the board's answer on the page.
    function show(d) {
      el('t').textContent = d.temp;
      el('h').textContent = d.hum;
      el('p').textContent = d.pres;
      el('card').textContent = d.card ? 'LOGGING' : 'NO CARD';
      el('card').className = 'badge ' + (d.card ? 'ok' : 'fail');
      el('detail').textContent = d.card_detail;
      el('rows').textContent = d.rows;
      el('size').textContent = d.size < 1024 ? d.size + ' bytes' : (d.size / 1024).toFixed(1) + ' kB';
      el('last').textContent = d.last || 'none yet';
      if (document.activeElement !== el('interval')) el('interval').value = d.interval;
      el('clock').textContent = d.clock;
      el('clockstate').textContent = d.clock_set ? 'SET' : 'NOT SET';
      el('clockstate').className = 'badge ' + (d.clock_set ? 'ok' : 'warn');
    }
    async function post(path, query) {
      try {
        const r = await fetch(path + '?' + query, { method: 'POST' });
        show(await r.json());
      } catch (e) { /* board busy or out of range: the next refresh catches up */ }
    }
    async function refresh() {
      try { show(await (await fetch('/data')).json()); } catch (e) {}
    }
    // Date.now() is milliseconds since 1 Jan 1970. getTimezoneOffset() is in
    // minutes and has the opposite sign to the usual "UTC+1", hence the minus.
    post('/settime', 'epoch=' + Math.floor(Date.now() / 1000) + '&offset=' + (-new Date().getTimezoneOffset()));
    setInterval(refresh, 2000);
  </script>
</body>
</html>
)HTML";
