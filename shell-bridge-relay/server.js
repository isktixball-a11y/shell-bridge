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

const wss = new WebSocketServer({ server, path: "/ws" });

let camera = null;
const viewers = new Set();

wss.on("connection", (ws, req) => {
  const isCamera = req.url.includes("camera");
  if (isCamera) {
    camera = ws;
    console.log("📷 Camera connected");
    ws.on("message", (data) => {
      for (const v of viewers) v.send(data);
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
