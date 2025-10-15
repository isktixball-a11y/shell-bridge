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

// Server keep-alive settings
server.keepAliveTimeout = 65000;
server.headersTimeout = 66000;

const wss = new WebSocketServer({ server, path: "/ws" });

let camera = null;
const viewers = new Set();

// Heartbeat interval to keep connections alive
const heartbeatInterval = setInterval(() => {
  wss.clients.forEach((ws) => {
    if (ws.isAlive === false) {
      console.log("⚠️ Client timeout, terminating");
      return ws.terminate();
    }
    ws.isAlive = false;
    ws.ping(); // Send ping frame
  });
}, 30000); // 30 second heartbeat

wss.on("connection", (ws, req) => {
  ws.isAlive = true;

  // Pong handler - client responds to ping
  ws.on("pong", () => {
    ws.isAlive = true;
  });

  // Error handler
  ws.on("error", (error) => {
    console.error("❌ WebSocket error:", error.message);
  });

  const isCamera = req.url.includes("camera");
  
  if (isCamera) {
    camera = ws;
    console.log("📷 Camera connected");
    
    ws.on("message", (data) => {
      try {
        for (const v of viewers) {
          if (v.readyState === 1) { // 1 = OPEN
            v.send(data, (err) => {
              if (err) console.error("Send error:", err.message);
            });
          }
        }
      } catch (err) {
        console.error("Message broadcast error:", err.message);
      }
    });

    ws.on("close", () => {
      camera = null;
      console.log("❌ Camera disconnected");
    });
  } else {
    viewers.add(ws);
    console.log(`👀 Viewer joined (${viewers.size})`);
    
    ws.on("close", () => {
      viewers.delete(ws);
      console.log(`👋 Viewer left (${viewers.size})`);
    });
  }
});

// Cleanup on shutdown
process.on("SIGTERM", () => {
  console.log("🛑 SIGTERM received, closing...");
  clearInterval(heartbeatInterval);
  wss.clients.forEach((ws) => ws.close());
  server.close();
});