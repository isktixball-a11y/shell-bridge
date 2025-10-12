import http from "http";
import { WebSocketServer } from "ws";
import * as jpeg from "jpeg-js";
import wrtc from "@roamhq/wrtc";

const PORT = process.env.PORT || 10000;
const { nonstandard } = wrtc;
const videoSource = new nonstandard.RTCVideoSource();

let viewerCount = 0; // track if anyone is watching

const server = http.createServer((req, res) => {
  if (req.url === "/status") {
    // viewer status endpoint for ESP32
    res.writeHead(200, { "Content-Type": "text/plain" });
    res.end(viewerCount > 0 ? "VIEWER=1" : "VIEWER=0");
  } else {
    res.writeHead(200);
    res.end("OK");
  }
});

// --- Upload channel (ESP32 sends JPEG frames) ---
const wssUpload = new WebSocketServer({ noServer: true });
wssUpload.on("connection", (ws) => {
  console.log("[upload] ESP32 connected");
  ws.on("message", (data) => {
    try {
      const decoded = jpeg.decode(Buffer.from(data), { useTArray: true });
      videoSource.onFrame({
        width: decoded.width,
        height: decoded.height,
        data: decoded.data,
      });
    } catch (err) {
      console.error("Decode error:", err.message);
    }
  });
  ws.on("close", () => console.log("[upload] ESP32 disconnected"));
});

// --- Viewer WebRTC signaling ---
const wssSignal = new WebSocketServer({ noServer: true });
wssSignal.on("connection", (ws) => {
  console.log("[signal] viewer connected");
  viewerCount++;

  const pc = new wrtc.RTCPeerConnection({
    iceServers: [{ urls: ["stun:stun.l.google.com:19302"] }],
  });
  const track = videoSource.createTrack();
  pc.addTrack(track);
  pc.onicecandidate = ({ candidate }) => {
    if (candidate) ws.send(JSON.stringify({ candidate }));
  };

  ws.on("message", async (msg) => {
    const data = JSON.parse(msg);
    if (data.offer) {
      await pc.setRemoteDescription(new wrtc.RTCSessionDescription(data.offer));
      const answer = await pc.createAnswer();
      await pc.setLocalDescription(answer);
      ws.send(JSON.stringify({ answer }));
    } else if (data.candidate) {
      await pc.addIceCandidate(data.candidate);
    }
  });

  ws.on("close", () => {
    try {
      track.stop();
    } catch {}
    pc.close();
    viewerCount--;
    console.log("[signal] viewer disconnected");
  });
});

server.on("upgrade", (req, socket, head) => {
  if (req.url === "/upload") {
    wssUpload.handleUpgrade(req, socket, head, (ws) =>
      wssUpload.emit("connection", ws, req)
    );
  } else if (req.url === "/signal") {
    wssSignal.handleUpgrade(req, socket, head, (ws) =>
      wssSignal.emit("connection", ws, req)
    );
  } else {
    socket.destroy();
  }
});

server.listen(PORT, () => console.log("bridge up on :" + PORT));
