import http from "http";
import { WebSocketServer } from "ws";
import * as jpeg from "jpeg-js";
import wrtc from "@roamhq/wrtc";

const PORT = process.env.PORT || 8080;
const { nonstandard } = wrtc;

let videoSource = null; // create later, once resolution is known
let viewerCount = 0;
let frameCount = 0;

// === HTTP STATUS ===
const server = http.createServer((req, res) => {
  if (req.url === "/status") {
    res.writeHead(200, { "Content-Type": "text/plain" });
    res.end(viewerCount > 0 ? "VIEWER=1" : "VIEWER=0");
  } else {
    res.writeHead(200, { "Content-Type": "text/plain" });
    res.end("OK");
  }
});

// === Upload WS (ESP32) ===
const wssUpload = new WebSocketServer({ noServer: true });
wssUpload.on("connection", (ws) => {
  console.log("[upload] ESP32 connected");

  ws.on("message", (data) => {
    try {
      const buf = Buffer.from(data);
      if (buf.length < 200) return;

      const decoded = jpeg.decode(buf, { useTArray: true });
      if (!decoded || !decoded.width || !decoded.height) {
        console.warn("⚠️ Invalid JPEG skipped");
        return;
      }

      // Initialize videoSource lazily on first frame
      if (!videoSource) {
        videoSource = new nonstandard.RTCVideoSource();
        console.log(
          `🟩 Initialized RTCVideoSource for ${decoded.width}x${decoded.height}`
        );
      }

      // Convert RGBA → RGB
      const rgba = decoded.data;
      const rgb = Buffer.alloc(decoded.width * decoded.height * 3);
      for (let i = 0, j = 0; i < rgba.length; i += 4, j += 3) {
        rgb[j] = rgba[i];
        rgb[j + 1] = rgba[i + 1];
        rgb[j + 2] = rgba[i + 2];
      }

      // Send frame to WebRTC
      videoSource.onFrame({
        width: decoded.width,
        height: decoded.height,
        data: rgb,
      });

      frameCount++;
      if (frameCount % 5 === 0)
        console.log(`📸 Frame OK: ${decoded.width}x${decoded.height} (${buf.length} bytes)`);

    } catch (err) {
      console.error("❌ Decode error:", err.message);
    }
  });

  ws.on("close", () => console.log("[upload] ESP32 disconnected"));
});

// === Viewer WS (WebRTC) ===
const wssSignal = new WebSocketServer({ noServer: true });
wssSignal.on("connection", (ws) => {
  console.log("[signal] viewer connected");
  viewerCount++;

  const pc = new wrtc.RTCPeerConnection({
    iceServers: [{ urls: ["stun:stun.l.google.com:19302"] }],
  });

  // Add track dynamically when videoSource is ready
  let track = null;
  const tryAddTrack = () => {
    if (videoSource && !track) {
      track = videoSource.createTrack();
      pc.addTrack(track);
      console.log("🎬 Added live track to viewer");
    }
  };
  tryAddTrack();
  const interval = setInterval(tryAddTrack, 1000); // re-check until ready

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
    clearInterval(interval);
    try { track?.stop(); } catch {}
    pc.close();
    viewerCount--;
    console.log("[signal] viewer disconnected");
  });
});

// === Upgrade HTTP → WS ===
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
