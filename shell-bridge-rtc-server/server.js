import express from "express";
import http from "http";
import { WebSocketServer } from "ws";

const app = express();
const server = http.createServer(app);
const PORT = process.env.PORT || 10000;

// cameraId -> Set<WebSocket> (viewers)
const viewers = new Map();
// cameraId -> WebSocket (exactly one camera per id)
const cameras = new Map();

function getViewers(id) {
  if (!viewers.has(id)) viewers.set(id, new Set());
  return viewers.get(id);
}

// Health & simple docs
app.get("/", (_req, res) => {
  res.send(`<h1>SHELL-Bridge WS</h1>
  <p>Viewer: <code>/viewer.html?cameraId=DEMO-001</code></p>`);
});

// Serve the static viewer
app.get("/viewer.html", (req, res) => {
  res.sendFile(new URL("./viewer.html", import.meta.url).pathname);
});

const wss = new WebSocketServer({ noServer: true });

// Simple auth: require ?key=... for camera connections (optional for viewers)
const CAMERA_KEY = process.env.CAMERA_KEY || "supersecret123";

server.on("upgrade", (req, socket, head) => {
  // expect url like /ws/camera/DEMO-001?key=... or /ws/view/DEMO-001
  const url = new URL(req.url, `http://${req.headers.host}`);
  const parts = url.pathname.split("/").filter(Boolean); // ["ws","camera","DEMO-001"]
  if (parts[0] !== "ws" || parts.length < 3) {
    socket.destroy();
    return;
  }
  const role = parts[1];       // "camera" or "view"
  const cameraId = parts[2];   // "DEMO-001"

  wss.handleUpgrade(req, socket, head, (ws) => {
    ws.role = role;
    ws.cameraId = cameraId;
    ws.isAlive = true;
    ws.on("pong", () => (ws.isAlive = true));

    // --- CAMERA ---
    if (role === "camera") {
      const key = url.searchParams.get("key");
      if (key !== CAMERA_KEY) {
        ws.close(1008, "unauthorized");
        return;
      }
      // Only one camera per id; close previous if any
      if (cameras.has(cameraId)) {
        try { cameras.get(cameraId).close(1000, "replaced"); } catch {}
      }
      cameras.set(cameraId, ws);

      ws.on("message", (data) => {
        // Expect binary JPEG frames
        if (typeof data === "string") return; // ignore text
        const set = getViewers(cameraId);
        for (const v of set) {
          try { v.send(data, { binary: true }); } catch {}
        }
      });

      ws.on("close", () => {
        if (cameras.get(cameraId) === ws) cameras.delete(cameraId);
      });
    }

    // --- VIEWER ---
    else if (role === "view") {
      getViewers(cameraId).add(ws);
      ws.on("close", () => getViewers(cameraId).delete(ws));
    }
  });
});

// keep-alive ping
setInterval(() => {
  for (const ws of wss.clients) {
    if (!ws.isAlive) { try { ws.terminate(); } catch {} continue; }
    ws.isAlive = false;
    try { ws.ping(); } catch {}
  }
}, 15000);

server.listen(PORT, () => {
  console.log("shell-bridge-rtc-server listening on", PORT);
});
