/*
 * ============================================================
 *  ORB DISPLAY  —  ESP32-C3  +  SH1106/SSD1306 I2C OLED
 *  Monochromatic Orb Display v1.0
 *
 *  Required Libraries (install via Arduino Library Manager):
 *   • U8g2          by olikraus          (>=2.35.x)
 *   • ESPAsyncWebServer by ESP32Async     (>=3.x)
 *   • AsyncTCP      by ESP32Async        (>=3.x)
 *   • ArduinoJson   by Benoit Blanchon   (>=7.x)
 *
 *  Board: ESP32-C3 (Arduino-ESP32 core >=3.x)
 *  Core builtins used: WiFi, Preferences, time.h
 * ============================================================
 */

#ifndef ARDUINO_ARCH_ESP32
  #error "This sketch is for ESP32 only."
#endif

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <ESPmDNS.h>
#include <time.h>
#include <math.h>
#include <WiFiClientSecure.h>

#include "magic8.h"
// ─── User Configuration ────────────────────────────────────
#define WIFI_SSID      "##########"
#define WIFI_PASSWORD  "######"
#define HOSTNAME       "orb"          // mDNS: orb.local

// ─── I²C Pins (ESP32-C3 defaults; adjust if needed) ────────
#define SDA_PIN  20
#define SCL_PIN  10
#define TOUCH_PIN D1               // GPIO3 on XIAO ESP32-C3

// ─── Display Driver ────────────────────────────────────────
// Comment/uncomment to match your hardware:
U8G2_SH1106_128X64_NONAME_F_HW_I2C  u8g2(U8G2_R0, U8X8_PIN_NONE);
// U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ─── NTP / Time ────────────────────────────────────────────
#define NTP_SERVER1   "pool.ntp.org"
#define NTP_SERVER2   "time.google.com"
#define TZ_OFFSET_SEC  0   // UTC

// ─── Web Server ────────────────────────────────────────────
AsyncWebServer  server(80);
static volatile bool g_tleFetching = false;
// ─── Persistent storage ────────────────────────────────────
Preferences prefs;

// ─── Viewport calibration ──────────────────────────────────
int16_t  g_cx     = 64;   // center_x  (0-128)
int16_t  g_cy     = 32;   // center_y  (0-64)
int16_t  g_radius = 28;   // viewport radius (10-64)

// ─── Operating mode ────────────────────────────────────────
enum class Mode : uint8_t { SOLAR = 0, RADAR = 1, TEXT = 2, MAGIC8 = 3 };
volatile Mode g_mode = Mode::SOLAR;

// ─── Speed warp (solar system) ─────────────────────────────
volatile float g_speed = 100.0f;   // default 100x real-time

// ─── Custom text ───────────────────────────────────────────
static char g_customText[128] = "ORB DISPLAY ONLINE";

// ─── Radar target ──────────────────────────────────────────
volatile uint8_t g_radarTarget = 0;   // 0=ISS, 1=Hubble, 2=Tiangong

// ─── TLE storage ───────────────────────────────────────────
struct TLE {
  char name[24];
  char line1[70];
  char line2[70];
  bool valid;
};
static TLE g_tles[3];

// ─── Flags ─────────────────────────────────────────────────
volatile bool g_timeSynced = false;

// ─── Timing ────────────────────────────────────────────────
static uint32_t t_frame    = 0;
static uint32_t t_tleFetch = 0;
static uint32_t t_ntpRetry = 0;

constexpr uint32_t FRAME_MS      =  33;        // ~30 FPS
constexpr uint32_t TLE_FETCH_MS  = 3600000UL;  // 1 hour
constexpr uint32_t NTP_RETRY_MS  = 30000UL;

// ─── Forward declarations ───────────────────────────────────
void loadPreferences();
void savePreferences();
void setupWiFi();
void setupNTP();
void setupMDNS_service();
void setupServer();
void fetchTLEs();
void renderFrame();
void drawSolarSystem();
void drawRadar();
void drawCustomText();
bool isInsideViewport(int16_t x, int16_t y);
void drawClippedPixel(int16_t x, int16_t y);
void drawClippedLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
void drawViewportRing();
void drawStarfield(uint32_t tick);
void drawSunCore(int16_t cx, int16_t cy, uint32_t tick);
void drawDashedCircle(int16_t cx, int16_t cy, int16_t r, uint8_t dashLen, uint8_t gapLen);
void drawSolidOrbitCircle(int16_t cx, int16_t cy, int16_t r);
void parseTLEResponse(const char* body, uint8_t slot);
void parseTLEOrbit(uint8_t slot);
void calcSatelliteAzEl(uint8_t slot, float timeSec, float& azOut, float& elOut);
void animateLock(uint32_t tick, const int16_t px[], const int16_t py[], const float angles[]);

// Include web UI (generated HTML stored in flash)
#include "web_ui.h"


static bool g_orbParsed[3] = {false, false, false};

constexpr uint32_t DEBOUNCE_MS     = 50;  // Minimum stable signal duration
constexpr uint32_t DOUBLE_TAP_GAP  = 400; // Max time allowed between taps (ms)

// ─── Non-Blocking Double-Touch Handler ──────────────────────
void checkTouchSensor() {
  static bool lastReading = LOW;
  static bool debouncedState = LOW;
  static uint32_t lastDebounceTime = 0;
  static uint32_t lastTapTime = 0;
  static uint8_t tapCount = 0;

  bool currentReading = digitalRead(TOUCH_PIN);

  // 1. Debounce Check
  if (currentReading != lastReading) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
    // State has stabilized
    if (currentReading != debouncedState) {
      debouncedState = currentReading;

      // 2. Rising Edge Detected (Touch Active)
      if (debouncedState == HIGH) {
        uint32_t now = millis();

        if (now - lastTapTime <= DOUBLE_TAP_GAP) {
          tapCount++;
        } else {
          tapCount = 1; // First tap or window expired
        }
        lastTapTime = now;

        // 3. Double Touch Confirmed
        if (tapCount >= 2) {
          tapCount = 0; // Reset counter
          
          // Change mode and trigger fresh re-roll
          g_mode = Mode::MAGIC8;
          triggerMagic8Ball();
          prefs.putUChar("mode", (uint8_t)g_mode); // Save state
        }
      }
    }
  }

  lastReading = currentReading;
}
// ═══════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println(F("\n\n=== ORB DISPLAY BOOT ==="));
  pinMode(SDA_PIN, INPUT_PULLUP);
  pinMode(SCL_PIN, INPUT_PULLUP);
  pinMode(TOUCH_PIN, INPUT);
  delay(10);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);   // Drops speed to 100kHz so bad traces don't drop frames
  Wire.setTimeOut(100);
  u8g2.begin();
  u8g2.setContrast(200);
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(4, 16, "ORB DISPLAY v1.0");
  u8g2.drawStr(4, 28, "Initialising...");
  u8g2.sendBuffer();

  loadPreferences();
  setupWiFi();
  setupNTP();
  setupMDNS_service();
  setupServer();

  t_tleFetch = millis() - TLE_FETCH_MS;  // trigger immediate fetch
  t_frame    = millis();
  t_ntpRetry = millis();

  Serial.println(F("=== BOOT COMPLETE ==="));
}
void tleFetchTask(void *pvParameters) {
  g_tleFetching = true;
  fetchTLEs();
  g_tleFetching = false;
  vTaskDelete(NULL);
}
// ═══════════════════════════════════════════════════════════
//  LOOP  (fully non-blocking millis() state machine)
// ═══════════════════════════════════════════════════════════
void loop() {
  checkTouchSensor();
  uint32_t now = millis();

  // NTP retry
  if (!g_timeSynced && (now - t_ntpRetry >= NTP_RETRY_MS)) {
    t_ntpRetry = now;
    struct tm ti;
    if (getLocalTime(&ti, 500)) g_timeSynced = true;
  }

  // TLE fetch (once per hour, briefly blocking ~1-2 s total)
  // ─── FreeRTOS Task Creation Fix ────────────────────────────
  // Increase stack size from 4096 to 8192 bytes in loop()[cite: 1]
  if (now - t_tleFetch >= TLE_FETCH_MS) {
    t_tleFetch = now;
    if (!g_tleFetching) {
      xTaskCreatePinnedToCore(
        tleFetchTask,
        "tleFetchTask",
        8192,  // Increased to prevent stack overflow during WiFiClient calls[cite: 1]
        NULL,
        1,
        NULL,
        0
      );
    }
  }


  // Frame render
  if (now - t_frame >= FRAME_MS) {
    t_frame = now;
    renderFrame();
  }
}

// ═══════════════════════════════════════════════════════════
//  PREFERENCES
// ═══════════════════════════════════════════════════════════
void loadPreferences() {
  prefs.begin("orb", true);
  g_cx           = (int16_t)prefs.getInt("cx", 64);
  g_cy           = (int16_t)prefs.getInt("cy", 32);
  g_radius       = (int16_t)prefs.getInt("r",  28);
  uint8_t m      = prefs.getUChar("mode", 0);
  if (m <= 2) g_mode = (Mode)m;
  g_speed        = prefs.getFloat("spd", 100.0f);
  g_radarTarget  = prefs.getUChar("rt", 0);
  size_t len = prefs.getString("txt", g_customText, sizeof(g_customText));
  if (len == 0) strlcpy(g_customText, "ORB DISPLAY ONLINE", sizeof(g_customText));
  prefs.end();

  g_cx          = constrain(g_cx, 0, 128);
  g_cy          = constrain(g_cy, 0, 64);
  g_radius      = constrain(g_radius, 10, 64);
  g_speed       = constrain(g_speed, 1.0f, 50000.0f);
  g_radarTarget = constrain(g_radarTarget, (uint8_t)0, (uint8_t)2);

  Serial.printf("Prefs: cx=%d cy=%d r=%d mode=%d spd=%.0f\n",
                g_cx, g_cy, g_radius, (int)g_mode, g_speed);
}

void savePreferences() {
  prefs.begin("orb", false);
  prefs.putInt("cx",    g_cx);
  prefs.putInt("cy",    g_cy);
  prefs.putInt("r",     g_radius);
  prefs.putUChar("mode", (uint8_t)g_mode);
  prefs.putFloat("spd", g_speed);
  prefs.putUChar("rt",  g_radarTarget);
  prefs.putString("txt", g_customText);
  prefs.end();
}

// ═══════════════════════════════════════════════════════════
//  WIFI
// ═══════════════════════════════════════════════════════════
void setupWiFi() {
  WiFi.persistent(false);    // Prevents unnecessary NVS flash writes
  WiFi.disconnect(true);     // Wipes cached credentials/state
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print(F("Connecting WiFi"));
  uint8_t attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500); Serial.print('.'); attempts++;
  }
  // Replace your existing "WiFi FAILED" block with this:
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi FAILED – Starting SoftAP...");
    
    WiFi.mode(WIFI_AP);
    WiFi.softAP("OrbDisplay-Setup", "12345678"); // Network Name & Password
    
    Serial.print("Access Point Started! IP: ");
    Serial.println(WiFi.softAPIP()); // Prints 192.168.4.1
  }
}

// ═══════════════════════════════════════════════════════════
//  NTP
// ═══════════════════════════════════════════════════════════
void setupNTP() {
  configTime(TZ_OFFSET_SEC, 0, NTP_SERVER1, NTP_SERVER2);
  struct tm ti;
  if (getLocalTime(&ti, 4000)) {
    g_timeSynced = true;
    Serial.println(F("NTP synced"));
  } else {
    Serial.println(F("NTP pending — will retry"));
  }
}

// ═══════════════════════════════════════════════════════════
//  mDNS
// ═══════════════════════════════════════════════════════════
void setupMDNS_service() {
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS: http://%s.local\n", HOSTNAME);
  }
}

// ═══════════════════════════════════════════════════════════
//  WEB SERVER
// ═══════════════════════════════════════════════════════════
// ─── Web Server Endpoint & Body Handler Fix ────────────────
void setupServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send_P(200, "text/html", WEB_UI_HTML);
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *req) {
    struct tm ti;
    bool hasTime = getLocalTime(&ti, 100);
    char dateBuf[32] = "NO TIME";
    if (hasTime) strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d %H:%M:%S UTC", &ti);

    StaticJsonDocument<384> doc;
    doc["mode"]        = (uint8_t)g_mode;
    doc["cx"]          = g_cx;
    doc["cy"]          = g_cy;
    doc["radius"]      = g_radius;
    doc["speed"]       = g_speed;
    doc["radarTarget"] = g_radarTarget;
    doc["customText"]  = g_customText;
    doc["utcTime"]     = dateBuf;
    doc["timeSynced"]  = g_timeSynced;
    doc["freeHeap"]    = (uint32_t)ESP.getFreeHeap();
    doc["ip"]          = WiFi.localIP().toString();
    JsonArray tleArr   = doc["tleLoaded"].to<JsonArray>();
    for (uint8_t i = 0; i < 3; i++) tleArr.add(g_tles[i].valid);

    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });
  server.on("/shake", HTTP_POST, [](AsyncWebServerRequest *req) {
    g_mode = Mode::MAGIC8;
    triggerMagic8Ball();
    req->send(200, "text/plain", "8-Ball Shaken");
  });
  // Safe HTTP POST Body Handler (No mid-stream Flash writes)[cite: 1]
  auto bodyHandler = [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
    // Only process single-chunk JSON bodies; reject fragmented payloads
    if (index != 0 || len != total) {
      if (index + len >= total) {
        req->send(400, "text/plain", "Payload too large or fragmented");
      }
      return;
    }

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
      req->send(400, "text/plain", "Bad JSON");
      return;
    }

    String url = req->url();
    if (url == "/calibrate") {
      if (doc["cx"].is<int>())     g_cx     = constrain((int)doc["cx"],     0, 128);
      if (doc["cy"].is<int>())     g_cy     = constrain((int)doc["cy"],     0,  64);
      if (doc["radius"].is<int>()) g_radius = constrain((int)doc["radius"], 10, 64);
      // NOTE: savePreferences() removed to protect Flash lifespan during live slider drags
    } else if (url == "/mode") {
      if (doc["mode"].is<int>()) {
        Mode newMode = (Mode)constrain((int)doc["mode"], 0, 3);

        // If switching into 8-Ball mode, re-roll the answer and reset the timer
        if (newMode == Mode::MAGIC8) {
          triggerMagic8Ball();
        }

        g_mode = newMode;
        prefs.putUChar("mode", (uint8_t)g_mode); // Save state
      }
    }else if (url == "/speed") {
      if (doc["speed"].is<float>()) {
        float s = doc["speed"];
        g_speed = constrain(s, 1.0f, 50000.0f);
      }
    } else if (url == "/radar") {
      if (doc["target"].is<int>()) {
        int t = doc["target"];
        g_radarTarget = constrain(t, 0, 2);
      }
    } else if (url == "/text") {
      if (doc["text"].is<const char*>()) {
        strlcpy(g_customText, doc["text"].as<const char*>(), sizeof(g_customText));
      }
    }
    req->send(200, "text/plain", "OK");
  };

  server.on("/calibrate", HTTP_POST, [](AsyncWebServerRequest*){}, nullptr, bodyHandler);
  server.on("/mode",      HTTP_POST, [](AsyncWebServerRequest*){}, nullptr, bodyHandler);
  server.on("/speed",     HTTP_POST, [](AsyncWebServerRequest*){}, nullptr, bodyHandler);
  server.on("/radar",     HTTP_POST, [](AsyncWebServerRequest*){}, nullptr, bodyHandler);
  server.on("/text",      HTTP_POST, [](AsyncWebServerRequest*){}, nullptr, bodyHandler);

  // Dedicated explicit endpoint to write to NVS Flash safely[cite: 1]
  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *req) {
    savePreferences();
    req->send(200, "text/plain", "Preferences Saved to Flash");
  });

  server.on("/fetchtle", HTTP_POST, [](AsyncWebServerRequest *req) {
    t_tleFetch = millis() - TLE_FETCH_MS;
    req->send(200, "text/plain", "TLE fetch queued");
  });

  server.onNotFound([](AsyncWebServerRequest *req) {
    req->send(404, "text/plain", "Not found");
  });

  server.begin();
  Serial.println(F("HTTP server started :80"));
}
// ═══════════════════════════════════════════════════════════
//  TLE FETCH  (synchronous WiFiClient, ~1-2 s total, hourly)
// ═══════════════════════════════════════════════════════════

static String httpGet(const char* host, const char* path) {
  if (WiFi.status() != WL_CONNECTED) return String();
  WiFiClientSecure client;
  client.setInsecure(); // Skip TLS certificate verification for simplicity
  
  if (!client.connect(host, 443)) return String();
  client.printf("GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: OrbDisplay/1.0\r\nConnection: close\r\n\r\n",
                path, host);
  String response;
  uint32_t deadline = millis() + 6000;
  bool headerDone = false;
  while ((client.connected() || client.available()) && millis() < deadline) {
    while (client.available()) {
      String line = client.readStringUntil('\n');
      if (!headerDone) {
        if (line == "\r" || line == "") headerDone = true;
      } else {
        response += line + '\n';
        if (response.length() > 2048) goto done;
      }
    }
  }
  done:
  client.stop();
  return response;
}

void parseTLEResponse(const char* body, uint8_t slot) {
  if (!body || strlen(body) < 40) return;
  char lines[3][80];
  int  lineCount = 0;
  const char* p = body;
  while (*p && lineCount < 3) {
    char tmp[80]; size_t i = 0;
    while (*p && *p != '\n' && i < 79) tmp[i++] = *p++;
    tmp[i] = '\0';
    if (*p == '\n') p++;
    if (i > 0 && tmp[i-1] == '\r') tmp[--i] = '\0';
    if (i > 0) { strlcpy(lines[lineCount], tmp, 80); lineCount++; }
  }
  if (lineCount < 3) return;
  strlcpy(g_tles[slot].name,  lines[0], 24);
  strlcpy(g_tles[slot].line1, lines[1], 70);
  strlcpy(g_tles[slot].line2, lines[2], 70);
  g_tles[slot].valid = true;
  g_orbParsed[slot]  = false;
  Serial.printf("TLE[%d] OK: %s\n", slot, g_tles[slot].name);
}

void fetchTLEs() {
  if (WiFi.status() != WL_CONNECTED) return;
  Serial.println(F("Fetching TLEs from CelesTrak..."));

  struct { const char* catnr; uint8_t slot; } targets[] = {
    { "25544", 0 },   // ISS
    { "20580", 1 },   // Hubble
    { "48274", 2 },   // Tiangong-2 / CSS
  };

  for (auto& t : targets) {
    char path[80];
    snprintf(path, sizeof(path), "/NORAD/elements/gp.php?CATNR=%s&FORMAT=3LE", t.catnr);
    String resp = httpGet("celestrak.org", path);
    if (resp.length() > 40) parseTLEResponse(resp.c_str(), t.slot);
    else Serial.printf("TLE[%d] fetch failed\n", t.slot);
  }
}

// ═══════════════════════════════════════════════════════════
//  VIEWPORT HELPERS
// ═══════════════════════════════════════════════════════════

inline bool isInsideViewport(int16_t x, int16_t y) {
  int32_t dx = x - g_cx, dy = y - g_cy;
  return (dx*dx + dy*dy) <= ((int32_t)g_radius * g_radius);
}

inline void drawClippedPixel(int16_t x, int16_t y) {
  if (x >= 0 && x < 128 && y >= 0 && y < 64 && isInsideViewport(x, y))
    u8g2.drawPixel(x, y);
}

// Bresenham with per-pixel viewport clipping
void drawClippedLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  int16_t dx = abs(x1-x0), sx = x0 < x1 ? 1 : -1;
  int16_t dy = -abs(y1-y0), sy = y0 < y1 ? 1 : -1;
  int16_t err = dx + dy;
  for (;;) {
    drawClippedPixel(x0, y0);
    if (x0 == x1 && y0 == y1) break;
    int16_t e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void drawViewportRing() {
  u8g2.drawCircle(g_cx, g_cy, g_radius, U8G2_DRAW_ALL);
}

// ─── Starfield ─────────────────────────────────────────────
static const int8_t STAR_DX[] = {-12,  5, -7, 18,-20,  3, 15, -8, 22,-15,  9, -3};
static const int8_t STAR_DY[] = { -8, 14, -6,  5,  9,-12,  3, 18, -5, 12, -7, 20};
static const uint8_t NUM_STARS = sizeof(STAR_DX);

void drawStarfield(uint32_t tick) {
  for (uint8_t i = 0; i < NUM_STARS; i++) {
    int16_t sx = g_cx + STAR_DX[i];
    int16_t sy = g_cy + STAR_DY[i];
    if (!isInsideViewport(sx, sy)) continue;
    uint8_t phase = (tick >> (i & 3)) & 0x0F;
    if (phase < 12) u8g2.drawPixel(sx, sy);   // 75% duty
  }
}

// ─── Sun ───────────────────────────────────────────────────
void drawSunCore(int16_t cx, int16_t cy, uint32_t tick) {
  // 3×3 solid core
  for (int8_t dx = -1; dx <= 1; dx++)
    for (int8_t dy = -1; dy <= 1; dy++)
      drawClippedPixel(cx+dx, cy+dy);

  // Dithered checkerboard glow at r≤3
  for (int8_t dx = -3; dx <= 3; dx++) {
    for (int8_t dy = -3; dy <= 3; dy++) {
      if (abs(dx) <= 1 && abs(dy) <= 1) continue;
      if ((dx*dx + dy*dy) > 11) continue;
      if (((dx + dy + (int8_t)(tick >> 2)) & 1) == 0)
        drawClippedPixel(cx+dx, cy+dy);
    }
  }

  // 8 animated solar flare rays
  float wobble = sinf((float)(tick & 0xFF) * 0.05f) * 8.0f;
  for (uint8_t i = 0; i < 8; i++) {
    float ang  = (i * 45.0f + wobble) * (M_PI / 180.0f);
    uint8_t rl = (i & 1) ? 5 : 7;
    int16_t ex = cx + (int16_t)(cosf(ang) * rl);
    int16_t ey = cy + (int16_t)(sinf(ang) * rl);
    // Draw only tip pixel for concise ray
    drawClippedPixel(cx + (int16_t)(cosf(ang) * 4), cy + (int16_t)(sinf(ang) * 4));
    drawClippedPixel(ex, ey);
  }
}

// ─── Dashed circle ─────────────────────────────────────────
void drawDashedCircle(int16_t cx, int16_t cy, int16_t r,
                      uint8_t dashLen, uint8_t gapLen) {
  uint8_t total = dashLen + gapLen;
  for (uint16_t a = 0; a < 360; a++) {
    if ((a % total) < dashLen) {
      float rad = a * (M_PI / 180.0f);
      int16_t px = cx + (int16_t)(cosf(rad) * r);
      int16_t py = cy + (int16_t)(sinf(rad) * r);
      drawClippedPixel(px, py);
    }
  }
}

// ─── Solid orbit circle ────────────────────────────────────
void drawSolidOrbitCircle(int16_t cx, int16_t cy, int16_t r) {
  u8g2.drawCircle(cx, cy, r, U8G2_DRAW_ALL);
}

// ═══════════════════════════════════════════════════════════
//  KEPLERIAN PLANET MATH
//  Mean Longitude: L = L0 + L1_per_day * d  (degrees)
//  d = days since J2000.0 (2000-01-01 12:00 UTC)
//  Source: Schlyter's simplified planetary model
// ═══════════════════════════════════════════════════════════

struct PlanetData {
  const char* name;
  float  L0;          // Mean longitude at J2000.0 (deg)
  float  L1;          // Daily motion (deg/day)
  float  sqrtA;       // sqrt(semi-major axis in AU) for scaling
};

static const PlanetData PLANETS[] = {
  { "MER", 252.25084f, 4.09233445f, 0.6225f },
  { "VEN", 181.97973f, 1.60213034f, 0.8503f },
  { "EAR", 100.46457f, 0.98560028f, 1.0000f },
  { "MAR", 355.45332f, 0.52402068f, 1.2345f },
};
static const uint8_t NUM_PLANETS = 4;

static float daysSinceJ2000(const struct tm& t) {
  int y = t.tm_year + 1900, m = t.tm_mon + 1, d = t.tm_mday;
  int jdn = 367*y - (7*(y+(m+9)/12))/4 + (275*m)/9 + d + 1721013;
  float jd = jdn + (t.tm_hour + t.tm_min/60.0f + t.tm_sec/3600.0f)/24.0f - 0.5f;
  return jd - 2451545.0f;
}

// ─── Saturn-style bitmap for Mars ──────────────────────────
static const uint8_t SAT_BMP[3][7] = {
  { 0,1,1,1,1,1,0 },
  { 1,1,1,1,1,1,1 },
  { 0,1,1,1,1,1,0 },
};
void drawSaturnBitmap(int16_t px, int16_t py) {
  for (int8_t r = 0; r < 3; r++)
    for (int8_t c = 0; c < 7; c++)
      if (SAT_BMP[r][c])
        drawClippedPixel(px - 3 + c, py - 1 + r);
}

// ═══════════════════════════════════════════════════════════
//  TARGET LOCK ANIMATION
// ═══════════════════════════════════════════════════════════
static uint32_t lockTick   = 0;
static uint8_t  lockTarget = 0;
static float    lockFX     = 64.0f, lockFY = 32.0f;

void animateLock(uint32_t tick,
                 const int16_t px[], const int16_t py[],
                 const float angles[]) {
  if ((tick % 180) == 0) {
    lockTarget = (lockTarget + 1) % NUM_PLANETS;
    lockTick   = tick;
  }

  uint8_t t = lockTarget;
  // Smooth pursuit
  lockFX += ((float)px[t] - lockFX) * 0.12f;
  lockFY += ((float)py[t] - lockFY) * 0.12f;

  int16_t lx = (int16_t)lockFX;
  int16_t ly = (int16_t)lockFY;
  if (!isInsideViewport(lx, ly)) return;

  // 5×5 corner lock-box
  const int8_t H = 2;
  int8_t corners[][4] = {{-H,-H,1,0},{-H,-H,0,1},{H,-H,-1,0},{H,-H,0,1},
                         {-H, H,1,0},{-H, H,0,-1},{H, H,-1,0},{H, H,0,-1}};
  for (auto& c : corners)
    drawClippedPixel(lx + c[0] + c[2], ly + c[1] + c[3]);
  // Thin line to planet
  drawClippedLine(lx, ly, px[t], py[t]);

  // Label
  char label[24];
  snprintf(label, sizeof(label), "%s %.0f\xB0", PLANETS[t].name, angles[t]);
  u8g2.setFont(u8g2_font_4x6_tf);
  int16_t lbx = lx + 5;
  int16_t lby = ly - 3;
  if (lbx + (int16_t)(strlen(label) * 4) > g_cx + g_radius) lbx = lx - (int16_t)(strlen(label)*4) - 2;
  if (lbx < 0) lbx = 0;
  u8g2.drawStr(lbx, lby, label);
}

// ═══════════════════════════════════════════════════════════
//  ZODIAC HELPER
// ═══════════════════════════════════════════════════════════
static const char* zodiacForDoy(int doy) {
  if (doy <  20) return "CAP"; if (doy <  50) return "AQR";
  if (doy <  80) return "PIS"; if (doy < 111) return "ARI";
  if (doy < 141) return "TAU"; if (doy < 172) return "GEM";
  if (doy < 204) return "CAN"; if (doy < 234) return "LEO";
  if (doy < 266) return "VIR"; if (doy < 296) return "LIB";
  if (doy < 326) return "SCO"; if (doy < 356) return "SAG";
  return "CAP";
}

// ═══════════════════════════════════════════════════════════
//  SIMPLIFIED SATELLITE ORBIT (circular approximation)
// ═══════════════════════════════════════════════════════════

struct OrbState {
  float inclDeg;
  float raanDeg;
  float meanMotion;   // rev/day
  bool  parsed;
};
static OrbState g_orbState[3] = {
  // ISS defaults if TLE not loaded yet
  { 51.64f, 0.0f, 15.49f, false },
  { 28.47f, 0.0f, 15.09f, false },
  { 41.47f, 0.0f, 15.60f, false },
};

void parseTLEOrbit(uint8_t slot) {
  if (!g_tles[slot].valid) return;
  const char* l2 = g_tles[slot].line2;
  size_t len = strlen(l2);
  if (len < 63) return;

  char buf[16];
  strncpy(buf, l2+8,  8); buf[8]  = '\0'; g_orbState[slot].inclDeg   = atof(buf);
  strncpy(buf, l2+17, 8); buf[8]  = '\0'; g_orbState[slot].raanDeg   = atof(buf);
  strncpy(buf, l2+52, 11); buf[11] = '\0'; g_orbState[slot].meanMotion = atof(buf);
  g_orbState[slot].parsed = true;
  Serial.printf("Orbit[%d] incl=%.1f raan=%.1f mm=%.3f\n",
                slot, g_orbState[slot].inclDeg,
                g_orbState[slot].raanDeg, g_orbState[slot].meanMotion);
}

void calcSatelliteAzEl(uint8_t slot, float timeSec, float& azOut, float& elOut) {
  if (!(g_orbState[slot].meanMotion > 0.0f)) {
    azOut = 0.0f;
    elOut = 0.0f;
    return;
  }
  float T_sec = (1440.0f / g_orbState[slot].meanMotion) * 60.0f;
  float phase = fmodf(timeSec, T_sec) / T_sec * 360.0f;
  azOut = fmodf(g_orbState[slot].raanDeg + phase, 360.0f);
  elOut = g_orbState[slot].inclDeg * sinf(phase * (M_PI / 180.0f));
}

// ═══════════════════════════════════════════════════════════
//  RENDER DISPATCH
// ═══════════════════════════════════════════════════════════
static uint32_t g_frameTick = 0;

void renderFrame() {
  u8g2.clearBuffer();
  switch (g_mode) {
    case Mode::SOLAR:  drawSolarSystem(); break;
    case Mode::RADAR:  drawRadar();       break;
    case Mode::TEXT:   drawCustomText();  break;
    case Mode::MAGIC8: drawMagic8Ball(u8g2, g_cx, g_cy, g_radius, drawClippedLine); break;
  }
  u8g2.sendBuffer();
  g_frameTick++;
}

// ═══════════════════════════════════════════════════════════
//  MODE 1 — SOLAR SYSTEM ORRERY
// ═══════════════════════════════════════════════════════════
static int16_t g_planetX[NUM_PLANETS];
static int16_t g_planetY[NUM_PLANETS];
static float   g_planetAngle[NUM_PLANETS];
static float   g_moonAngle = 0.0f;

void drawSolarSystem() {
  uint32_t tick = g_frameTick;

  struct tm ti;
  bool hasTime = g_timeSynced && getLocalTime(&ti, 5);
  // Base day + simulated warp offset accumulated via millis
  static float warpAccum = 0.0f;
  warpAccum += (g_speed / (86400000.0f / FRAME_MS));  // days per frame at warp
  if (warpAccum > 36525.0f) warpAccum = fmodf(warpAccum, 36525.0f);
  float baseDays = hasTime ? daysSinceJ2000(ti) : 0.0f;
  float warpedD = baseDays + warpAccum;

  // Orbit radii: scale so outermost orbit fits at 92% of radius
  float rScale = (float)g_radius * 0.92f / PLANETS[NUM_PLANETS-1].sqrtA;

  drawStarfield(tick);

  // Orbits
  for (uint8_t i = 0; i < NUM_PLANETS; i++) {
    int16_t orbitR = (int16_t)(PLANETS[i].sqrtA * rScale);
    if (orbitR < 1) orbitR = 1;
    if (i < 2) {
      drawSolidOrbitCircle(g_cx, g_cy, orbitR);
    } else {
      drawDashedCircle(g_cx, g_cy, orbitR, 4, 3);
    }
    // Radial tick marks on outermost orbit
    if (i == NUM_PLANETS - 1) {
      for (uint16_t a = 0; a < 360; a += 30) {
        float rad = a * (M_PI / 180.0f);
        int16_t ix = g_cx + (int16_t)(cosf(rad) * (orbitR - 2));
        int16_t iy = g_cy + (int16_t)(sinf(rad) * (orbitR - 2));
        int16_t ox = g_cx + (int16_t)(cosf(rad) * (orbitR + 2));
        int16_t oy = g_cy + (int16_t)(sinf(rad) * (orbitR + 2));
        drawClippedLine(ix, iy, ox, oy);
      }
    }
  }

  drawSunCore(g_cx, g_cy, tick);

  // Planets
  for (uint8_t i = 0; i < NUM_PLANETS; i++) {
    float L = fmodf(PLANETS[i].L0 + PLANETS[i].L1 * warpedD, 360.0f);
    if (L < 0) L += 360.0f;
    g_planetAngle[i] = L;
    float rad   = L * (M_PI / 180.0f);
    int16_t orbitR = (int16_t)(PLANETS[i].sqrtA * rScale);
    g_planetX[i] = g_cx + (int16_t)(cosf(rad) * orbitR);
    g_planetY[i] = g_cy + (int16_t)(sinf(rad) * orbitR);

    if (!isInsideViewport(g_planetX[i], g_planetY[i])) continue;

    if (i == 3) {
      // Mars: Saturn-like micro-bitmap
      drawSaturnBitmap(g_planetX[i], g_planetY[i]);
    } else {
      u8g2.drawPixel(g_planetX[i], g_planetY[i]);
      // Larger dot for Earth
      if (i == 2) {
        u8g2.drawPixel(g_planetX[i]+1, g_planetY[i]);
        u8g2.drawPixel(g_planetX[i], g_planetY[i]+1);
      }
    }

    // Earth–Moon system
    if (i == 2) {
      g_moonAngle = fmodf(g_moonAngle + 0.45f + g_speed * 0.004f, 360.0f);
      float mr = g_moonAngle * (M_PI / 180.0f);
      int16_t mx = g_planetX[i] + (int16_t)(cosf(mr) * 3.5f);
      int16_t my = g_planetY[i] + (int16_t)(sinf(mr) * 3.5f);
      drawClippedPixel(mx, my);
    }
  }

  drawViewportRing();

  // HUD telemetry header (inside viewport top)
  u8g2.setFont(u8g2_font_4x6_tf);
  if (hasTime) {
    char hud[32];
    snprintf(hud, sizeof(hud), "%04d-%02d-%02d %s",
             ti.tm_year+1900, ti.tm_mon+1, ti.tm_mday,
             zodiacForDoy(ti.tm_yday+1));
    int16_t hx = g_cx - 30;
    int16_t hy = g_cy - g_radius + 7;
    if (hy < 6)  hy = 6;
    if (hx < 0)  hx = 0;
    u8g2.drawStr(hx, hy, hud);
  }

  // Target lock
  animateLock(tick, g_planetX, g_planetY, g_planetAngle);
}

// ═══════════════════════════════════════════════════════════
//  MODE 2 — SATELLITE RADAR
// ═══════════════════════════════════════════════════════════
static float g_sweepAngle = 0.0f;

#define TRAIL_LEN 24
static float   g_trailAngles[TRAIL_LEN] = {0};
static uint8_t g_trailHead = 0;



void drawRadar() {
  uint32_t tick = g_frameTick;
  uint8_t  slot = g_radarTarget;

  if (g_tles[slot].valid && !g_orbParsed[slot]) {
    parseTLEOrbit(slot);
    g_orbParsed[slot] = true;
  }

  float timeSec = (float)(millis() / 1000UL);
  float satAz, satEl;
  calcSatelliteAzEl(slot, timeSec, satAz, satEl);

  // Concentric rings at 25/50/75% of radius
  for (uint8_t ring = 1; ring <= 3; ring++)
    u8g2.drawCircle(g_cx, g_cy, (g_radius * ring) / 4, U8G2_DRAW_ALL);
  drawViewportRing();

  // Cross-hair
  drawClippedLine(g_cx - g_radius, g_cy, g_cx + g_radius, g_cy);
  drawClippedLine(g_cx, g_cy - g_radius, g_cx, g_cy + g_radius);

  // Store sweep in trail ring buffer
  g_trailAngles[g_trailHead] = g_sweepAngle;
  g_trailHead = (g_trailHead + 1) % TRAIL_LEN;

  // Draw trail (older = fewer pixels = "fade")
  for (uint8_t t = 0; t < TRAIL_LEN; t++) {
    if (t > TRAIL_LEN - 4) continue;   // skip oldest
    float tAng = g_trailAngles[(g_trailHead - 1 - t + TRAIL_LEN) % TRAIL_LEN];
    float trad = tAng * (M_PI / 180.0f);
    uint8_t step = 1 + t / 5;          // increasing step = sparser = dimmer
    int16_t ex = g_cx + (int16_t)(cosf(trad) * g_radius);
    int16_t ey = g_cy + (int16_t)(sinf(trad) * g_radius);
    int16_t dx = ex - g_cx, dy = ey - g_cy;
    int16_t len = (int16_t)sqrtf((float)(dx*dx + dy*dy));
    if (len == 0) continue;
    for (int16_t s = 0; s < len; s += step) {
      int16_t px = g_cx + (int16_t)((float)dx * s / len);
      int16_t py = g_cy + (int16_t)((float)dy * s / len);
      drawClippedPixel(px, py);
    }
  }

  // Sweep line
  float srad = g_sweepAngle * (M_PI / 180.0f);
  drawClippedLine(g_cx, g_cy,
                  g_cx + (int16_t)(cosf(srad) * g_radius),
                  g_cy + (int16_t)(sinf(srad) * g_radius));
  g_sweepAngle = fmodf(g_sweepAngle + 3.0f, 360.0f);

  // Satellite blip
  float satRad = satAz * (M_PI / 180.0f);
  float satR   = (float)g_radius * (1.0f - constrain(satEl, 0.0f, 90.0f) / 90.0f);
  int16_t satX = g_cx + (int16_t)(cosf(satRad) * satR);
  int16_t satY = g_cy + (int16_t)(sinf(satRad) * satR);

  if (isInsideViewport(satX, satY)) {
    for (int8_t dx = -1; dx <= 1; dx++)
      for (int8_t dy = -1; dy <= 1; dy++)
        drawClippedPixel(satX+dx, satY+dy);

    const char* satName = g_tles[slot].valid ? g_tles[slot].name : "DEMO";
    u8g2.setFont(u8g2_font_4x6_tf);
    int16_t lx = g_cx - (int16_t)(strlen(satName) * 2);
    int16_t ly = g_cy + g_radius - 6;
    if (ly > 62) ly = 62;
    u8g2.drawStr((lx < 0) ? 0 : lx, ly, satName);

    char azBuf[20];
    snprintf(azBuf, sizeof(azBuf), "AZ%3.0f EL%2.0f", satAz, satEl);
    u8g2.drawStr((g_cx - 20 < 0) ? 0 : g_cx - 20,
                 (g_cy + g_radius - 13 < 0) ? 0 : g_cy + g_radius - 13,
                 azBuf);
  }

  // Origin dot
  u8g2.drawPixel(g_cx, g_cy);

  // Compass labels
  u8g2.setFont(u8g2_font_4x6_tf);
  u8g2.drawStr(g_cx-2, g_cy - g_radius + 6, "N");
  u8g2.drawStr(g_cx + g_radius - 5, g_cy + 2, "E");
  (void)tick;
}

// ═══════════════════════════════════════════════════════════
//  MODE 3 — CUSTOM TEXT HUD
// ═══════════════════════════════════════════════════════════
static int16_t  g_textScroll    = 0;
static uint32_t g_lastScrollMs  = 0;

void drawCustomText() {
  uint32_t tick = g_frameTick;
  uint32_t now  = millis();

  drawViewportRing();
  drawDashedCircle(g_cx, g_cy, g_radius - 3, 3, 2);
  u8g2.drawCircle(g_cx, g_cy, g_radius - 6, U8G2_DRAW_ALL);

  // Rotating corner accent triangles
  float triAng = (float)(tick % 720) * 0.5f * (M_PI / 180.0f);
  for (uint8_t q = 0; q < 4; q++) {
    float a = triAng + q * (M_PI / 2.0f);
    float r1 = (float)(g_radius - 7);
    float r2 = (float)(g_radius - 11);
    int16_t ax = g_cx + (int16_t)(cosf(a) * r1);
    int16_t ay = g_cy + (int16_t)(sinf(a) * r1);
    int16_t bx = g_cx + (int16_t)(cosf(a + 0.3f) * r2);
    int16_t by = g_cy + (int16_t)(sinf(a + 0.3f) * r2);
    int16_t cx2= g_cx + (int16_t)(cosf(a - 0.3f) * r2);
    int16_t cy2= g_cy + (int16_t)(sinf(a - 0.3f) * r2);
    drawClippedLine(ax, ay, bx, by);
    drawClippedLine(ax, ay, cx2, cy2);
    drawClippedLine(bx, by, cx2, cy2);
  }

  // Text scrolling
  u8g2.setFont(u8g2_font_5x7_tf);
  uint16_t textPxW = (uint16_t)(strlen(g_customText) * 5);
  int16_t  clipW   = (g_radius <= 8) ? 0 : (g_radius - 8) * 2;
  uint8_t  clipH   = 10;

  if (now - g_lastScrollMs > 80) {
    g_lastScrollMs = now;
    if (textPxW > clipW) {
      g_textScroll++;
      if (g_textScroll > (int16_t)textPxW + (int16_t)clipW)
        g_textScroll = -(int16_t)clipW;
    } else {
      g_textScroll = 0;
    }
  }

  int16_t clipX1 = max((int16_t)0,   (int16_t)(g_cx - clipW/2));
  int16_t clipY1 = max((int16_t)0,   (int16_t)(g_cy - clipH/2));
  int16_t clipX2 = min((int16_t)127, (int16_t)(g_cx + clipW/2));
  int16_t clipY2 = min((int16_t)63,  (int16_t)(g_cy + clipH/2));
  u8g2.setClipWindow(clipX1, clipY1, clipX2, clipY2);
  u8g2.drawStr(g_cx - clipW/2 - g_textScroll, g_cy + 3, g_customText);
  u8g2.setMaxClipWindow();

  // Cardinal dots
  for (uint8_t i = 0; i < 4; i++) {
    float ang = (i * 90 + 45) * (M_PI / 180.0f);
    drawClippedPixel(g_cx + (int16_t)(cosf(ang)*(g_radius-4)),
                     g_cy + (int16_t)(sinf(ang)*(g_radius-4)));
  }
}

// ─── end of orb_display.ino ────────────────────────────────
