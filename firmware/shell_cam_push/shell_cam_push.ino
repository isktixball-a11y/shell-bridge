#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>

// ====== WIFI ======
const char* WIFI_SSID = "24k";
const char* WIFI_PASS = "Boringotfam_00";

// ====== RENDER INGEST ======
const char* INGEST_URL = "https://shell-bridge.onrender.com/ingest?key=supersecret123"; // change to your URL + key

// ====== CAMERA PINS (ESP32-S3-EYE) ======
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

// ====== TUNABLES ======
static const framesize_t FRAME_SIZE = FRAMESIZE_QVGA; // QVGA or VGA for latency
static const int JPEG_QUALITY = 12;                    // lower = better quality, higher = faster
static const int PUSH_INTERVAL_MS = 80;                // ~12 fps target
unsigned long lastPush = 0;

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
  c.xclk_freq_hz = 20000000;   // 20 MHz
  c.frame_size = FRAME_SIZE;
  c.pixel_format = PIXFORMAT_JPEG;
  c.grab_mode = CAMERA_GRAB_LATEST;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.jpeg_quality = JPEG_QUALITY;
  c.fb_count = 2; // double buffer

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    for(;;) delay(1000);
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.printf("\nWiFi connected: %s\n", WiFi.localIP().toString().c_str());
  initCamera();
}

bool pushFrame(camera_fb_t* fb) {
  if (!fb || fb->format != PIXFORMAT_JPEG) return false;

  HTTPClient http;
  http.begin(INGEST_URL);
  String boundary = "----shellboundary";
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

  // Build multipart body
  WiFiClient *stream = http.getStreamPtr();
  if (!http.sendRequest("POST", (uint8_t*)nullptr, 0)) {
    http.end(); return false;
  }

  String head = "--" + boundary + "\r\n"
                "Content-Disposition: form-data; name=\"frame\"; filename=\"f.jpg\"\r\n"
                "Content-Type: image/jpeg\r\n\r\n";
  stream->print(head);
  stream->write(fb->buf, fb->len);
  stream->print("\r\n--" + boundary + "--\r\n");

  int code = http.writeToStream(stream); // flush
  (void)code;
  int status = http.getSize(); // not used, but forces headers read
  http.end();
  return true;
}

void loop() {
  unsigned long now = millis();
  if (now - lastPush < PUSH_INTERVAL_MS) return;
  lastPush = now;

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return;

  bool ok = pushFrame(fb);
  esp_camera_fb_return(fb);

  if (!ok) {
    Serial.println("Push failed");
    delay(200);
  }
}
