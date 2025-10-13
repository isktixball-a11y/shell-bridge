import http from "http";
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";
import { WebSocketServer } from "ws";

const PORT = process.env.PORT || 10000;
const __dirname = path.dirname(fileURLToPath(import.meta.url));

let latestImage = null; // keep latest snapshot in memory

// --- HTTP Server ---
const server = http.createServer((req, res) => {
  if (req.url === "/upload" && req.method === "POST") {
    // Handle image upload
    let data = [];
    req.on("data", chunk => data.push(chunk));
    req.on("end", () => {
      latestImage = Buffer.concat(data);
      console.log(`📸 Received snapshot (${latestImage.length} bytes)`);

      // Optionally save to disk:
      fs.writeFileSync(path.join(__dirname, "latest.jpg"), latestImage);

      res.writeHead(200, { "Content-Type": "text/plain" });
      res.end("UPLOAD_OK");
    });
  } else if (req.url === "/latest.jpg" && latestImage) {
    // Serve last snapshot
    res.writeHead(200, { "Content-Type": "image/jpeg" });
    res.end(latestImage);
  } else if (req.url === "/") {
    res.writeHead(200, { "Content-Type": "text/html" });
    res.end(`<h2>🐣 SHELL Snapshot Server</h2><img src="/latest.jpg?${Date.now()}" width="640"/>`);
  } else {
    res.writeHead(404);
    res.end("Not found");
  }
});

server.listen(PORT, () => console.log(`📡 SHELL snapshot bridge running on port ${PORT}`));
