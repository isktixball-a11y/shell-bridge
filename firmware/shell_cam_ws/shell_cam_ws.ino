#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoWebsockets.h>  // install via Library Manager (ArduinoWebsockets by gilmaimon)

using namespace websockets;

// ===== WIFI =====
const char* WIFI_SSID = "24k";
const char* WIFI_PASS = "Boringotfam_00";

// ===== SERVER (Render) =====
// Use your Render host (same app where rtc-server is deployed)
const char* HOST = "shell-bridge.onrender.com";  // no protocol
const int   PORT = 443;                           // wss
const char* CAMERA_ID = "DEMO-001";
const char* CAMERA_KEY = "supersecret123";

// ===== CAMERA PINS (ESP32-S3-EYE) =====
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

// ===== SETTINGS =====
static const framesize_t FRAME_SIZE = FRAMESIZE_QVGA; // or VGA
static const int JPEG_QUALITY = 12;                   // 10–15 good
static const int FRAME_INTERVAL_MS = 80;              // ~12 fps
static const int WIFI_RETRY_MS = 5000;

unsigned long lastFrame = 0;
unsigned long lastWifiCheck = 0;

WebsocketsClient client;
WiFiClientSecure tls;

// ---- camera init ----
void initCamera() {
  camera_config_t c;
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;
  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;
  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;
  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;
  c.pin_d7 = Y9_GPIO_NUM;
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

// ---- wifi ----
void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.println("🔄 Connecting WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) {
    delay(400); Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n✅ WiFi connected: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n❌ WiFi failed");
  }
}

// ---- ws connect ----
bool connectWS() {
  if (client.available()) return true;

  // Build wss URL
  String path = "/ws/camera/" + String(CAMERA_ID) + "?key=" + String(CAMERA_KEY);
  String url = "wss://" + String(HOST) + ":" + String(PORT) + path;

  tls.setInsecure(); // skip cert validation for simplicity

  // Prepare client with secure transport
  client.setSSLClient(&tls, HOST, PORT);

  client.onEvent([](WebsocketsEvent e, String data){
    if (e == WebsocketsEvent::ConnectionOpened)  Serial.println("✅ WS connected");
    if (e == WebsocketsEvent::ConnectionClosed)  Serial.println("⚠️ WS closed");
    if (e == WebsocketsEvent::GotPing)           client.pong();
  });

  bool ok = client.connect(url);
  if (!ok) {
    Serial.println("❌ WS connect failed");
    return false;
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("=== SHELL Incubator Camera (WS) ===");
  ensureWiFi();
  initCamera();
}

void loop() {
  // keep WiFi alive
  if (WiFi.status() != WL_CONNECTED) {
    unsigned long now = millis();
    if (now - lastWifiCheck > WIFI_RETRY_MS) {
      lastWifiCheck = now;
      ensureWiFi();
    }
    delay(50);
    return;
  }

  // keep websocket alive
  if (!client.available()) {
    connectWS();
    delay(200);
  } else {
    client.poll();  // handle pings/pongs
  }

  // send frames
  unsigned long now = millis();
  if (now - lastFrame >= FRAME_INTERVAL_MS && client.available()) {
    lastFrame = now;
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) { delay(10); return; }

    // send as binary message
    bool ok = client.sendBinary((const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);

    if (!ok) {
      Serial.println("⚠️ send failed, reconnecting WS");
      client.close();
      delay(200);
    } else {
      // Serial.printf("📸 %d bytes\n", fb->len);
    }
  }
}
