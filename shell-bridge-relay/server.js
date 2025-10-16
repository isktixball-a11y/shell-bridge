import express from "express";
import { WebSocketServer } from "ws";

const app = express();
const PORT = process.env.PORT || 10000;

// Basic alive check
app.get("/", (req, res) => res.send("SHELL Relay Active ✅"));

// HTTP server for WebSocket upgrade
const server = app.listen(PORT, () =>
  console.log(`shell-relay listening on ${PORT}`)
);

// Keep-alive
server.keepAliveTimeout = 65000;
server.headersTimeout = 66000;

const wss = new WebSocketServer({ server, path: "/ws" });

// Global connections
let camera = null;
const viewers = new Set();

// Heartbeat every 30s
const heartbeatInterval = setInterval(() => {
  wss.clients.forEach((ws) => {
    if (ws.isAlive === false) {
      console.log("⚠️ Client timeout, terminating");
      return ws.terminate();
    }
    ws.isAlive = false;
    ws.ping(); // send ping frame
  });
}, 30000);

wss.on("connection", (ws, req) => {
  ws.isAlive = true;
  const ip = req.socket.remoteAddress;
  const isCamera = req.url.includes("camera");

  console.log(`🔗 New connection from ${ip} - ${isCamera ? "📷 CAMERA" : "👀 VIEWER"}`);

  // Handle pong responses
  ws.on("pong", () => (ws.isAlive = true));

  // Handle errors
  ws.on("error", (err) => {
    console.error("❌ WebSocket error:", err.message);
  });

  // ===== CAMERA CONNECTION =====
  if (isCamera) {
    camera = ws;
    console.log("📷 Camera connected ✅");

    ws.on("message", (data) => {
      console.log(`📨 Frame ${data.length} bytes → broadcasting to ${viewers.size} viewers`);
      for (const v of viewers) {
        if (v.readyState === 1) {
          v.send(data, (err) => {
            if (err) console.error("Send error:", err.message);
          });
        }
      }
    });

    ws.on("close", () => {
      camera = null;
      console.log("❌ Camera disconnected");
      // Notify all viewers camera is offline
      for (const v of viewers) {
        if (v.readyState === 1) v.send("CAMERA_OFF");
      }
    });

    return;
  }

  // ===== VIEWER CONNECTION =====
  viewers.add(ws);
  console.log(`👀 Viewer joined (${viewers.size})`);

  // Notify camera that a viewer is connected
  if (camera && camera.readyState === 1) {
    camera.send("VIEWER_ON");
    console.log("📡 Sent VIEWER_ON to camera");
  }

  // Handle viewer disconnect
  ws.on("close", () => {
    viewers.delete(ws);
    console.log(`👋 Viewer left (${viewers.size})`);
    if (viewers.size === 0 && camera && camera.readyState === 1) {
      camera.send("VIEWER_OFF");
      console.log("📡 Sent VIEWER_OFF to camera");
    }
  });
});

// Cleanup on shutdown
process.on("SIGTERM", () => {
  console.log("🛑 SIGTERM received, closing...");
  clearInterval(heartbeatInterval);
  wss.clients.forEach((ws) => ws.close());
  server.close();
});
