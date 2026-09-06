// See relay.h. Adapted from the esp32-board-app-template relay for the PoC (uses the
// ble_hid control handler + its own NVS settings; no config.cpp here).
#include "relay.h"
#include "ble_hid.h"
#include "netota.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

static Preferences   s_pref;
static String        s_url, s_tok, s_id;
static volatile bool s_active = false;
static volatile int  s_state  = 0;             // 0 off, 1 connecting (WiFi down), 2 online (polling)
static TaskHandle_t  s_task   = nullptr;
static QueueHandle_t s_cmdQ   = nullptr;       // task -> main loop  (pulled commands)
static QueueHandle_t s_replyQ = nullptr;       // main loop -> task  (replies to POST)
struct RelayMsg { char s[560]; };              // PoC commands can be long (file-write chunks / scripts)

static bool isHttps() { return s_url.startsWith("https"); }

static void computeId() {
  String m = bleHidMac() ? String(bleHidMac()) : String("000000");
  m.replace(":", "");
  if (m.length() > 6) m = m.substring(m.length() - 6);
  s_id = "bt-" + m;
}
const char* relayId() { if (!s_id.length()) computeId(); return s_id.c_str(); }
bool relayActive() { return s_active; }
int  relayState()  { return s_state; }
String relayGetUrl()   { return s_url; }
String relayGetToken() { return s_tok; }
bool   relayGetAuto()  { s_pref.begin("relay", true); bool a = s_pref.getBool("auto", false); s_pref.end(); return a; }
void   relaySetAuto(bool on) { s_pref.begin("relay", false); s_pref.putBool("auto", on); s_pref.end(); }
bool   relayGetKeep()  { s_pref.begin("relay", true); bool k = s_pref.getBool("keep", false); s_pref.end(); return k; }
void   relaySetKeep(bool on) { s_pref.begin("relay", false); s_pref.putBool("keep", on); s_pref.end(); }

// ---- HTTP helpers — ONLY ever called from the relay task, so at most one TLS context
// exists at a time (a no-PSRAM board can't fit two TLS + WiFi + BLE in heap). ----
static String httpPull() {                       // GET /pull — modest timeout so idle cycles stay short
  String u = s_url + "/pull/" + s_id, out;
  HTTPClient http; http.setTimeout(6000);
  if (isHttps()) { WiFiClientSecure c; c.setInsecure(); if (!http.begin(c, u)) return out;
    if (s_tok.length()) http.addHeader("x-relay-token", s_tok); if (http.GET() == 200) out = http.getString(); http.end(); }
  else { WiFiClient c; if (!http.begin(c, u)) return out;
    if (s_tok.length()) http.addHeader("x-relay-token", s_tok); if (http.GET() == 200) out = http.getString(); http.end(); }
  return out;
}
static bool httpPostReplyOnce(const String& u, const char* line) {
  HTTPClient http; http.setTimeout(8000); int code = 0;
  if (isHttps()) { WiFiClientSecure c; c.setInsecure(); if (!http.begin(c, u)) return false;
    if (s_tok.length()) http.addHeader("x-relay-token", s_tok); http.addHeader("Content-Type", "text/plain"); code = http.POST((uint8_t*)line, strlen(line)); http.end(); }
  else { WiFiClient c; if (!http.begin(c, u)) return false;
    if (s_tok.length()) http.addHeader("x-relay-token", s_tok); http.addHeader("Content-Type", "text/plain"); code = http.POST((uint8_t*)line, strlen(line)); http.end(); }
  return code == 200;
}

void relayPostReply(const char* line) {          // queue only; the task does the POST (one TLS at a time)
  if (!s_active || !s_replyQ) return;
  RelayMsg m; strlcpy(m.s, line, sizeof(m.s)); xQueueSend(s_replyQ, &m, 0);
}

static void relayTask(void*) {
  bool wasUp = false; uint32_t lastLog = 0;
  for (;;) {
    if (!s_active) { s_state = 0; wasUp = false; vTaskDelay(pdMS_TO_TICKS(400)); continue; }
    if (WiFi.status() != WL_CONNECTED) {
      s_state = 1; wasUp = false;
      if (millis() - lastLog > 3000) { lastLog = millis(); Serial.printf("[relay] waiting for WiFi (status=%d)\n", WiFi.status()); }
      vTaskDelay(pdMS_TO_TICKS(500)); continue;
    }
    if (!wasUp) { wasUp = true; s_state = 2; Serial.printf("[relay] WiFi up, IP %s — polling %s/pull/%s\n", WiFi.localIP().toString().c_str(), s_url.c_str(), s_id.c_str()); }
    RelayMsg m;
    while (xQueueReceive(s_replyQ, &m, 0)) { for (int i = 0; i < 4; i++) { if (httpPostReplyOnce(s_url + "/reply/" + s_id, m.s)) break; vTaskDelay(pdMS_TO_TICKS(150)); } }
    String cmd = httpPull();
    if (cmd.length()) { Serial.printf("[relay] cmd: %.40s\n", cmd.c_str()); RelayMsg c; strlcpy(c.s, cmd.c_str(), sizeof(c.s)); xQueueSend(s_cmdQ, &c, 0);
      vTaskDelay(pdMS_TO_TICKS(40)); }
    else vTaskDelay(pdMS_TO_TICKS(30));
  }
}

void relayBegin() {
  s_pref.begin("relay", true);
  s_url = s_pref.getString("url", ""); s_tok = s_pref.getString("tok", "");
  bool a = s_pref.getBool("auto", false);
  s_pref.end();
  if (a && s_url.length()) { Serial.println("[relay] auto-connect on boot"); relayGoRemote(s_url, s_tok); }
}

bool relayGoRemote(const String& url, const String& token) {
  s_url = url; s_url.trim(); while (s_url.endsWith("/")) s_url.remove(s_url.length() - 1);
  s_tok = token;
  s_pref.begin("relay", false); s_pref.putString("url", s_url); s_pref.putString("tok", s_tok); s_pref.end();
  computeId();
  s_state = 1;
  Serial.printf("[relay] go remote: %s as %s — bringing up WiFi STA\n", s_url.c_str(), s_id.c_str());
  netConnect();                                  // STA up with the saved WiFi creds
  s_active = true;
  if (!s_cmdQ)   s_cmdQ   = xQueueCreate(8, sizeof(RelayMsg));
  if (!s_replyQ) s_replyQ = xQueueCreate(8, sizeof(RelayMsg));
  if (!s_task)   xTaskCreatePinnedToCore(relayTask, "relay", 8192, nullptr, 1, &s_task, 0);
  return true;
}

void relayStop() { s_active = false; s_state = 0; Serial.println("[relay] stopped"); }

void relayTick() {
  if (!s_cmdQ) return;
  RelayMsg m;
  while (xQueueReceive(s_cmdQ, &m, 0)) bleHidHandleExternal(m.s);
}
