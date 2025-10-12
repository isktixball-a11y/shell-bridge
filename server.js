import http from "http";
import { WebSocketServer } from "ws";
import * as jpeg from "jpeg-js";
import wrtc from "@roamhq/wrtc";

const PORT = process.env.PORT || 8080;
const { nonstandard } = wrtc;
const videoSource = new nonstandard.RTCVideoSource();

let viewerCount = 0;
let frameCount = 0;

const server = http.createServer((req, res) => {
  if (req.url === "/status") {
    res.writeHead(200, { "Content-Type": "text/plain" });
    res.end(viewerCount > 0 ? "VIEWER=1" : "VIEWER=0");
  } else {
    res.writeHead(200, { "Content-Type": "text/plain" });
    res.end("OK");
  }
});

// === Upload WebSocket (ESP32 sends JPEG frames) ===
const wssUpload = new WebSocketServer({ noServer: true });
wssUpload.on("connection", (ws) => {
  console.log("[upload] ESP32 connected");

  ws.on("message", (data) => {
    try {
      const buf = Buffer.from(data);

      // Ignore empty or tiny frames
      if (buf.length < 100) {
        console.warn("⚠️ Dropped small payload:", buf.length);
        return;
      }

      // Decode JPEG
      const decoded = jpeg.decode(buf, { useTArray: true });
      if (!decoded || !decoded.width || !decoded.height) {
        console.warn("⚠️ Invalid JPEG skipped");
        return;
      }

      // Convert RGBA → RGB (remove alpha)
      const rgba = decoded.data;
      const rgb = Buffer.alloc(decoded.width * decoded.height * 3);
      for (let i = 0, j = 0; i < rgba.length; i += 4, j += 3) {
        rgb[j] = rgba[i];       // R
        rgb[j + 1] = rgba[i+1]; // G
        rgb[j + 2] = rgba[i+2]; // B
      }

      // Force correct VGA resolution (640×480)
      videoSource.onFrame({
        width: 640,
        height: 480,
        data: rgb,
      });

      frameCount++;
      if (frameCount % 5 === 0) {
        console.log(
          `📸 Frame OK: 640x480 (${buf.length} bytes)`
        );
      }
    } catch (err) {
      console.error("❌ Decode error:", err.message);
    }
  });

  ws.on("close", () => console.log("[upload] ESP32 disconnected"));
});

// === Viewer WebRTC signaling ===
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
      console.log("📡 Answer sent to viewer");
    } else if (data.candidate) {
      await pc.addIceCandidate(data.candidate);
    }
  });

  pc.onconnectionstatechange = () =>
    console.log(`🔄 RTC → ${pc.connectionState}`);

  ws.on("close", () => {
    try {
      track.stop();
    } catch {}
    pc.close();
    viewerCount--;
    console.log("[signal] viewer disconnected");
  });
});

// === Upgrade HTTP → WebSocket ===
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

server.listen(PORT, () =>
  console.log(`🚀 SHELL bridge running on port ${PORT}`)
);
