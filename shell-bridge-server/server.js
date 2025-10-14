import express from "express";
import Busboy from "busboy";

const app = express();

// Render sets PORT env; default for local run:
const PORT = process.env.PORT || 10000;

// In-memory latest frame
let latestFrame = null;
let latestTime = 0;

// Connected MJPEG clients
const clients = new Set();

// Ingest endpoint: ESP32 POSTs a JPEG frame as form-data field "frame"
app.post("/ingest", (req, res) => {
  const key = req.query.key || req.headers["x-key"];
  if (!process.env.INGEST_KEY || key !== process.env.INGEST_KEY) {
    return res.status(401).send("unauthorized");
  }
  const busboy = Busboy({ headers: req.headers });
  let gotFrame = false;

  busboy.on("file", (name, file, info) => {
    if (name !== "frame") return file.resume();
    const chunks = [];
    file.on("data", c => chunks.push(c));
    file.on("end", () => {
      latestFrame = Buffer.concat(chunks);
      latestTime = Date.now();
      // broadcast to all clients waiting on /stream
      for (const res of clients) {
        try {
          res.write(
            `--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ${latestFrame.length}\r\n\r\n`
          );
          res.write(latestFrame);
          res.write("\r\n");
        } catch (_) {}
      }
      gotFrame = true;
    });
  });

  busboy.on("finish", () => {
    if (!gotFrame) return res.status(400).send("no frame");
    res.status(200).send("ok");
  });

  req.pipe(busboy);
});

// MJPEG stream endpoint for viewers (Flutter/web)
app.get("/stream", (req, res) => {
  res.writeHead(200, {
    "Cache-Control": "no-cache, no-store, must-revalidate",
    Pragma: "no-cache",
    Expires: "0",
    "Connection": "close",
    "Content-Type": "multipart/x-mixed-replace; boundary=frame"
  });
  clients.add(res);

  // If we already have a frame, send one immediately
  if (latestFrame) {
    res.write(
      `--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ${latestFrame.length}\r\n\r\n`
    );
    res.write(latestFrame);
    res.write("\r\n");
  }

  req.on("close", () => clients.delete(res));
});

// Minimal test page (optional)
app.get("/", (_, res) => {
  res.redirect("/view.html");
});

app.get("/view.html", (_, res) => {
  res.sendFile(new URL("./view.html", import.meta.url).pathname);
});

app.listen(PORT, () => {
  console.log("shell-bridge listening on", PORT);
});
