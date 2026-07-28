// PillPilot — camera node.
//
// Waits for an MQTT "capture" trigger from the dispenser, takes a photo, and
// pushes it to the family Telegram group. Also serves an HTTP Basic-auth
// snapshot/stream on the LAN. Deliberately holds NO dosing logic — if this
// board crashes, dosing on the other board is unaffected.
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "esp_camera.h"

#include "config.h"
#include "secrets.h"

static WebServer      httpd(80);
#if MQTT_USE_TLS
  static WiFiClientSecure mqttNet;
#else
  static WiFiClient     mqttNet;
#endif
static PubSubClient    mqtt(mqttNet);

// ── Camera init ───────────────────────────────────────────────────────────────
static bool initCamera() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;  c.pin_d1 = Y3_GPIO_NUM;  c.pin_d2 = Y4_GPIO_NUM;
  c.pin_d3 = Y5_GPIO_NUM;  c.pin_d4 = Y6_GPIO_NUM;  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM;  c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM; c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM; c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM;  c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.jpeg_quality = JPEG_QUALITY;

  if (psramFound()) {
    c.frame_size = FRAMESIZE_SVGA;   // 800x600 — plenty for a pill tray
    c.fb_count   = 2;
    c.fb_location = CAMERA_FB_IN_PSRAM;
    c.grab_mode  = CAMERA_GRAB_LATEST;
  } else {
    c.frame_size = FRAMESIZE_VGA;
    c.fb_count   = 1;
  }
  return esp_camera_init(&c) == ESP_OK;
}

static void ledStatus(bool on) { digitalWrite(PIN_LED_RED, on ? LOW : HIGH); } // active-low

// ── Push a JPEG to Telegram via multipart/form-data (TLS) ─────────────────────
static bool sendPhotoTelegram(camera_fb_t* fb, const String& caption) {
  WiFiClientSecure client;
  client.setInsecure();                       // see docs/SECURITY.md
  if (!client.connect("api.telegram.org", 443)) return false;

  String boundary = "----pillpilot" + String((uint32_t)esp_random(), HEX);
  String head =
      "--" + boundary + "\r\n"
      "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" + String(TELEGRAM_CHAT_ID) + "\r\n"
      "--" + boundary + "\r\n"
      "Content-Disposition: form-data; name=\"caption\"\r\n\r\n" + caption + "\r\n"
      "--" + boundary + "\r\n"
      "Content-Disposition: form-data; name=\"photo\"; filename=\"tray.jpg\"\r\n"
      "Content-Type: image/jpeg\r\n\r\n";
  String tail = "\r\n--" + boundary + "--\r\n";
  size_t contentLength = head.length() + fb->len + tail.length();

  client.printf("POST /bot%s/sendPhoto HTTP/1.1\r\n", TELEGRAM_BOT_TOKEN);
  client.print("Host: api.telegram.org\r\n");
  client.print("Content-Type: multipart/form-data; boundary=" + boundary + "\r\n");
  client.print("Content-Length: " + String(contentLength) + "\r\n");
  client.print("Connection: close\r\n\r\n");

  client.print(head);
  // Stream the image in chunks to keep RAM low.
  const size_t CHUNK = 1024;
  for (size_t i = 0; i < fb->len; i += CHUNK) {
    size_t n = min(CHUNK, fb->len - i);
    client.write(fb->buf + i, n);
  }
  client.print(tail);

  // Read the status line (best-effort).
  uint32_t t0 = millis();
  while (client.connected() && !client.available() && millis() - t0 < 8000) delay(10);
  String status = client.readStringUntil('\n');
  client.stop();
  return status.indexOf("200") > 0;
}

static void captureAndSend(const String& caption) {
  ledStatus(true);
#if USE_FLASH_ON_CAPTURE
  digitalWrite(PIN_LED_FLASH, HIGH); delay(120);
#endif
  camera_fb_t* fb = esp_camera_fb_get();
#if USE_FLASH_ON_CAPTURE
  digitalWrite(PIN_LED_FLASH, LOW);
#endif
  if (fb) {
    sendPhotoTelegram(fb, caption.length() ? caption : "PillPilot");
    esp_camera_fb_return(fb);
  }
  ledStatus(false);
}

// ── MQTT ──────────────────────────────────────────────────────────────────────
static void onMqtt(char* topic, uint8_t* payload, unsigned int len) {
  String msg; msg.reserve(len);
  for (unsigned i = 0; i < len; i++) msg += (char)payload[i];
  JsonDocument doc;
  String caption = "PillPilot";
  if (deserializeJson(doc, msg) == DeserializationError::Ok)
    caption = (const char*)(doc["caption"] | "PillPilot");
  captureAndSend(caption);
}

static void ensureMqtt() {
  static uint32_t last = 0;
  if (mqtt.connected() || millis() - last < 5000) return;
  last = millis();
  String cid = String(DEVICE_ID) + "-cam-" + String((uint32_t)esp_random(), HEX);
  if (mqtt.connect(cid.c_str(), MQTT_USER, MQTT_PASS)) mqtt.subscribe(TOPIC_CAM_CAPTURE);
}

// ── HTTP snapshot / stream (Basic auth) ───────────────────────────────────────
static bool requireAuth() {
  if (httpd.authenticate(CAM_HTTP_USER, CAM_HTTP_PASS)) return true;
  httpd.requestAuthentication();
  return false;
}
static void handleSnapshot() {
  if (!requireAuth()) return;
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { httpd.send(500, "text/plain", "capture failed"); return; }
  httpd.setContentLength(fb->len);
  httpd.send(200, "image/jpeg", "");
  httpd.client().write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}
static void handleStream() {
  if (!requireAuth()) return;
  WiFiClient client = httpd.client();
  client.print("HTTP/1.1 200 OK\r\n"
               "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n");
  // Low frame-rate stream; ESP32-CAM can't do much better reliably.
  while (client.connected()) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) break;
    client.printf("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
    client.write(fb->buf, fb->len);
    client.print("\r\n");
    esp_camera_fb_return(fb);
    delay(200);   // ~5 fps ceiling
  }
}

// ── Setup / loop ──────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED_RED, OUTPUT);   ledStatus(false);
  pinMode(PIN_LED_FLASH, OUTPUT); digitalWrite(PIN_LED_FLASH, LOW);

  if (!initCamera()) { Serial.println("[cam] init failed"); delay(3000); ESP.restart(); }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { delay(250); ledStatus((millis()/250)%2); }
  ledStatus(false);
  if (MDNS.begin(MDNS_HOSTNAME)) MDNS.addService("http", "tcp", 80);

#if MQTT_USE_TLS
  mqttNet.setInsecure();
#endif
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqtt);

  httpd.on("/snapshot", handleSnapshot);
  httpd.on("/stream",   handleStream);
  httpd.begin();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) { ensureMqtt(); mqtt.loop(); }
  httpd.handleClient();
}
