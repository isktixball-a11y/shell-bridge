#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// =======================
// 🔧 WiFi Config
// =======================
const char* WIFI_SSID = "24k";
const char* WIFI_PASS = "Boringotfam_00";

// =======================
// 🌐 Render Server Config
// =======================
const char* INGEST_URL = "https://shell-bridge.onrender.com/ingest?key=supersecret123";

// =======================
// 📸 Camera Pins (ESP32-S3-EYE)
// =======================
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

// =======================
// ⚙️ Capture Settings
// =======================
static const framesize_t FRAME_SIZE = FRAMESIZE_QVGA; // or FRAMESIZE_VGA
static const int JPEG_QUALITY = 12;                    // 10–15 = good balance
static const int PUSH_INTERVAL_MS = 80;                // ~12 fps
static const int WIFI_RETRY_DELAY_MS = 5000;           // retry delay on fail

unsigned long lastPush = 0;
unsigned long lastReconnectAttempt = 0;

// =======================
// 📷 Init Camera
// =======================
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
    for (;;)
      delay(1000);
  }
  Serial.println("✅ Camera initialized");
}

// =======================
// 🌐 WiFi Connect + Reconnect
// =======================
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println("🔄 Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  WiFi.setSleep(false);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    Serial.print(".");
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n✅ WiFi connected: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n❌ WiFi connect failed, will retry...");
  }
}

// =======================
// 📤 Push Frame (HTTPS Secure Upload)
// =======================
bool pushFrame(camera_fb_t* fb) {
  if (!fb || fb->format != PIXFORMAT_JPEG) return false;

  WiFiClientSecure client;
  client.setInsecure();                   // HTTPS without certificate
  HTTPClient http;
  http.setTimeout(10000);
  http.setConnectTimeout(10000);
  http.begin(client, INGEST_URL);

  // Build multipart pieces
  String boundary = "----shellboundary";
  String head = "--" + boundary + "\r\n"
                "Content-Disposition: form-data; name=\"frame\"; filename=\"frame.jpg\"\r\n"
                "Content-Type: image/jpeg\r\n\r\n";
  String tail = "\r\n--" + boundary + "--\r\n";

  size_t totalLen = head.length() + fb->len + tail.length();
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
  http.addHeader("Content-Length", String(totalLen));

  // Start POST request
  int code = http.sendRequest("POST");
  if (code <= 0) {
    Serial.printf("❌ HTTPS connect failed (%d)\n", code);
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  if (!stream) {
    Serial.println("❌ Stream pointer null");
    http.end();
    return false;
  }

  // Write the complete form body
  stream->print(head);
  stream->write(fb->buf, fb->len);
  stream->print(tail);
  stream->flush();             // ensure all bytes sent

  // Wait up to 5 s for server response
  unsigned long start = millis();
  while (stream->connected() && !stream->available() && millis() - start < 5000) delay(1);
  while (stream->available()) stream->read();

  http.end();
  Serial.printf("✅ Frame sent (%d bytes)\n", fb->len);
  return true;
}


// =======================
// 🚀 Setup
// =======================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== SHELL Incubator Camera ===");
  connectWiFi();
  initCamera();
}

// =======================
// 🔁 Loop
// =======================
void loop() {
  // Reconnect WiFi if dropped
  if (WiFi.status() != WL_CONNECTED) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > WIFI_RETRY_DELAY_MS) {
      lastReconnectAttempt = now;
      connectWiFi();
    }
    delay(100);
    return;
  }

  unsigned long now = millis();
  if (now - lastPush >= PUSH_INTERVAL_MS) {
    lastPush = now;
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("❌ Camera capture failed");
      delay(100);
      return;
    }

    bool ok = pushFrame(fb);
    esp_camera_fb_return(fb);

    // Retry logic if failed
    if (!ok) {
      Serial.println("⚠️ Push failed, reconnecting WiFi...");
      WiFi.disconnect();
      connectWiFi();
      delay(1000);
    }
  }
}
