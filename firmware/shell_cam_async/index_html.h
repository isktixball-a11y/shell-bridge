#pragma once
const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <title>SHELL Live (ESP32 WS)</title>
  <style>
    html,body{height:100%;margin:0;background:#0b0e11;color:#e6eef8;font-family:system-ui,Segoe UI,Roboto,Arial}
    .top{position:fixed;top:10px;left:12px;font-size:13px;color:#8ab4ff}
    .wrap{display:grid;place-items:center;height:100%}
    canvas{max-width:98vw;max-height:96vh;background:#000;image-rendering:auto;border-radius:12px;box-shadow:0 6px 20px rgba(0,0,0,.35)}
  </style>
</head>
<body>
  <div class="top" id="info">connecting…</div>
  <div class="wrap"><canvas id="c"></canvas></div>
  <script>
    const info = document.getElementById('info');
    const canvas = document.getElementById('c');
    const ctx = canvas.getContext('2d');
    const img = new Image();

    img.onload = () => {
      if (canvas.width !== img.naturalWidth || canvas.height !== img.naturalHeight) {
        canvas.width = img.naturalWidth;
        canvas.height = img.naturalHeight;
      }
      ctx.drawImage(img, 0, 0);
      URL.revokeObjectURL(img.src);
    };

    function connect() {
      const proto = (location.protocol === 'https:') ? 'wss' : 'ws';
      const url = proto + '://' + location.host + '/ws';
      const ws = new WebSocket(url);
      ws.binaryType = 'arraybuffer';
      ws.onopen = () => { info.textContent = 'live preview'; };
      ws.onmessage = (ev) => {
        const blob = new Blob([ev.data], { type: 'image/jpeg' });
        img.src = URL.createObjectURL(blob);
      };
      ws.onclose = () => { info.textContent = 'disconnected — reconnecting…'; setTimeout(connect, 1200); };
      ws.onerror = () => { try{ws.close();}catch(e){} };
    }
    connect();
  </script>
</body>
</html>
)HTML";
