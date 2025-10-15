#include <WiFi.h>
#include <esp_camera.h>
#include <WebSocketsClient.h>
#include <esp_crt_bundle.h>

// ================== CONFIG ==================
const char* WIFI_SSID = "24k";
const char* WIFI_PASS = "Boringotfam_00";

const char* WS_HOST   = "shell-bridge-relay.onrender.com";
const int   WS_PORT   = 443;                 // wss (SSL)
const char* WS_PATH   = "/ws?camera=DEMO-001";

#define CAMERA_MODEL_ESP32S3_EYE
// ============================================

// ========== CAMERA PINS (ESP32-S3-EYE) ==========
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  15
#define SIOD_GPIO_NUM  4
#define SIOC_GPIO_NUM  5

#define Y2_GPIO_NUM 11
#define Y3_GPIO_NUM 9
#define Y4_GPIO_NUM 8
#define Y5_GPIO_NUM 10
#define Y6_GPIO_NUM 12
#define Y7_GPIO_NUM 18
#define Y8_GPIO_NUM 17
#define Y9_GPIO_NUM 16

#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM  7
#define PCLK_GPIO_NUM  13
// =================================================

// Create WebSocket client
WebSocketsClient ws;

// ========== Initialize camera ==========
void initCamera() {
  camera_config_t c;
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM; c.pin_d1 = Y3_GPIO_NUM; c.pin_d2 = Y4_GPIO_NUM; c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM; c.pin_d5 = Y7_GPIO_NUM; c.pin_d6 = Y8_GPIO_NUM; c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM;
  c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM;
  c.pin_href  = HREF_GPIO_NUM;
  c.pin_sscb_sda = SIOD_GPIO_NUM;
  c.pin_sscb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn  = PWDN_GPIO_NUM;
  c.pin_reset = RESET_GPIO_NUM;

  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size   = FRAMESIZE_QVGA;  // try FRAMESIZE_VGA for higher res
  c.jpeg_quality = 12;              // 10–15 reasonable
  c.fb_count     = 2;
  c.fb_location  = CAMERA_FB_IN_PSRAM;
  c.grab_mode    = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("❌ Camera init failed: 0x%x\n", err);
    while (true) delay(1000);
  }
  Serial.println("✅ Camera initialized");
}

// ========== Wi-Fi ==========
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("🔄 Connecting WiFi");
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(400);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("\n✅ WiFi connected: %s\n", WiFi.localIP().toString().c_str());
  else
    Serial.println("\n❌ WiFi connect failed");
}

// ========== WebSocket event ==========
void wsEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.println("📡 Relay connected ✅");
      break;
    case WStype_DISCONNECTED:
      Serial.println("❌ Relay disconnected - retrying...");
      break;
    case WStype_ERROR:
      Serial.printf("❌ WebSocket error: %s\n", (char*)payload);
      break;
    default:
      break;
  }
}

// ========== Setup ==========
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== SHELL Incubator Camera (Render Relay) ===");

  connectWiFi();
  initCamera();

  // FIX: Load certificate bundle from flash
  Serial.println("🔐 Loading SSL certificates...");
  esp_crt_bundle_attach(NULL);

  // Configure WebSocket with SSL fixes
  ws.setExtraHeaders("User-Agent: ESP32-S3-CAM\r\n");
  ws.setReconnectInterval(3000);  // retry every 3s
  ws.beginSSL(WS_HOST, WS_PORT, WS_PATH);
  ws.onEvent(wsEvent);
  
  Serial.println("🔗 Connecting to relay...");
}

// ========== Loop ==========
void loop() {
  ws.loop();

  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    return;
  }

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("❌ Camera frame buffer error");
    return;
  }

  if (ws.isConnected()) {
    ws.sendBIN(fb->buf, fb->len);
    // Uncomment for debug:
    // Serial.printf("📸 Sent %u bytes\n", fb->len);
  }

  esp_camera_fb_return(fb);
  delay(200);  // ~5 FPS
}