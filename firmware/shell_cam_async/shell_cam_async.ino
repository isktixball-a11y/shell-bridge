#include "esp_camera.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "index_html.h"  // embedded HTML UI

// ===== WiFi =====
const char* WIFI_SSID = "24k";
const char* WIFI_PASS = "Boringotfam_00";

// ===== Camera Pins (ESP32-S3-EYE) =====
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  15
#define SIOD_GPIO_NUM  4
#define SIOC_GPIO_NUM  5
#define Y2_GPIO_NUM    11
#define Y3_GPIO_NUM    9
#define Y4_GPIO_NUM    8
#define Y5_GPIO_NUM    10
#define Y6_GPIO_NUM    12
#define Y7_GPIO_NUM    18
#define Y8_GPIO_NUM    17
#define Y9_GPIO_NUM    16
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM  7
#define PCLK_GPIO_NUM  13

// ===== Stream Settings =====
static const framesize_t FRAME_SIZE = FRAMESIZE_QVGA; // FRAMESIZE_VGA for sharper
static const int JPEG_QUALITY = 12;                    // 10–15 good
static const uint32_t FRAME_INTERVAL_MS = 80;          // ~12 fps target

// ===== Server & WebSocket =====
AsyncWebServer server(80);
AsyncWebSocket ws("/ws"); // binary JPEG frames go here

// Track connected clients (optional)
volatile uint32_t viewerCount = 0;

// ---- Camera init ----
void initCamera() {
  camera_config_t c;
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;

  c.pin_d0 = Y2_GPIO_NUM;   c.pin_d1 = Y3_GPIO_NUM;   c.pin_d2 = Y4_GPIO_NUM;   c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;   c.pin_d5 = Y7_GPIO_NUM;   c.pin_d6 = Y8_GPIO_NUM;   c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM;
  c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM;
  c.pin_href = HREF_GPIO_NUM;
  c.pin_sscb_sda = SIOD_GPIO_NUM;
  c.pin_sscb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM;
  c.pin_reset = RESET_GPIO_NUM;

  c.xclk_freq_hz = 20000000;
  c.frame_size = FRAME_SIZE;
  c.pixel_format = PIXFORMAT_JPEG;
  c.grab_mode = CAMERA_GRAB_LATEST;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.jpeg_quality = JPEG_QUALITY;
  c.fb_count = 2;

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("❌ Camera init failed: 0x%x\n", err);
    for(;;) delay(1000);
  }
  Serial.println("✅ Camera initialized");
}

// ---- WiFi connect ----
void connectWiFi() {
  Serial.println("🔄 Connecting WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(400);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n✅ WiFi connected: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n❌ WiFi failed, retrying next loop…");
  }
}

// ---- WebSocket Events ----
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
               void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    viewerCount++;
    Serial.printf("👀 Viewer %u connected (total=%u)\n", client->id(), viewerCount);
  } else if (type == WS_EVT_DISCONNECT) {
    if (viewerCount > 0) viewerCount--;
    Serial.printf("👋 Viewer %u disconnected (total=%u)\n", client->id(), viewerCount);
  } else if (type == WS_EVT_DATA) {
    // We don’t expect inbound data; ignore.
  } else if (type == WS_EVT_PONG) {
    // keepalive
  } else if (type == WS_EVT_ERROR) {
    Serial.printf("WS error on client %u\n", client->id());
  }
}

// ---- Setup ----
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== SHELL Incubator Camera (AsyncWebServer + WS) ===");

  connectWiFi();
  initCamera();

  // Serve UI
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send_P(200, "text/html", INDEX_HTML); // from index_html.h
  });

  // WebSocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // CORS (optional if you embed elsewhere)
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

  server.begin();
  Serial.println("🌐 Web UI: http://<device-ip>/  |  WS: /ws");
}

// ---- Loop: capture & broadcast ----
void loop() {
  // WiFi auto-reconnect
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    delay(500);
    return;
  }

  static uint32_t lastSend = 0;
  uint32_t now = millis();
  if (now - lastSend < FRAME_INTERVAL_MS) {
    delay(1);
    return;
  }
  lastSend = now;

  // Only capture if at least 1 viewer (optional optimization)
  if (viewerCount == 0) {
    delay(10);
    return;
  }

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("❌ Capture failed");
    delay(10);
    return;
  }
  if (fb->format != PIXFORMAT_JPEG) {
    esp_camera_fb_return(fb);
    Serial.println("❌ Not a JPEG frame");
    delay(10);
    return;
  }

  // Broadcast to all viewers as binary JPEG
  ws.binaryAll(fb->buf, fb->len);

  esp_camera_fb_return(fb);

  // Keep WebSocket service alive (ping every ~15s handled internally)
  ws.cleanupClients(); // remove dead clients

  // Optional: debug
  // Serial.printf("📸 sent %u bytes to %u viewers\n", fb->len, (uint32_t)viewerCount);
}
