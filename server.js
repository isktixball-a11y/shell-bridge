// WebRTC bridge for ESP32 JPEG frames
import http from 'http';
import { WebSocketServer } from 'ws';
import * as jpeg from 'jpeg-js';
import wrtc from '@roamhq/wrtc';

const PORT = process.env.PORT || 8080;
const { nonstandard } = wrtc;
const videoSource = new nonstandard.RTCVideoSource();

const server = http.createServer((req, res) => {
  res.writeHead(200);
  res.end('OK');
});

const wssUpload = new WebSocketServer({ noServer: true });
const wssSignal = new WebSocketServer({ noServer: true });

// ESP32 uploads JPEG frames
wssUpload.on('connection', (ws) => {
  console.log('[upload] ESP32 connected');
  ws.on('message', (data) => {
    try {
      const decoded = jpeg.decode(Buffer.from(data), { useTArray: true });
      videoSource.onFrame({
        width: decoded.width,
        height: decoded.height,
        data: decoded.data
      });
    } catch (err) {
      console.error('Decode error:', err.message);
    }
  });
  ws.on('close', () => console.log('[upload] ESP32 disconnected'));
});

// Viewer connects for WebRTC
wssSignal.on('connection', (ws) => {
  console.log('[signal] viewer connected');
  const pc = new wrtc.RTCPeerConnection({
    iceServers: [{ urls: ['stun:stun.l.google.com:19302'] }]
  });
  const track = videoSource.createTrack();
  pc.addTrack(track);
  pc.onicecandidate = ({ candidate }) => {
    if (candidate) ws.send(JSON.stringify({ candidate }));
  };
  ws.on('message', async (msg) => {
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
  ws.on('close', () => {
    try { track.stop(); } catch {}
    pc.close();
    console.log('[signal] viewer disconnected');
  });
});

// Upgrade HTTP → WS
server.on('upgrade', (req, socket, head) => {
  if (req.url === '/upload') {
    wssUpload.handleUpgrade(req, socket, head, (ws) => wssUpload.emit('connection', ws, req));
  } else if (req.url === '/signal') {
    wssSignal.handleUpgrade(req, socket, head, (ws) => wssSignal.emit('connection', ws, req));
  } else {
    socket.destroy();
  }
});

server.listen(PORT, () => console.log('bridge up on :' + PORT));
