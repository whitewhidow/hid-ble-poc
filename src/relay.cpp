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
#include "lwip/dns.h"    // dns_clear_cache() — a captive-portal AP poisons the DNS cache (host->its login IP);
                         // that entry survives the fallback to real WiFi, so we must flush it after switching.

static Preferences   s_pref;
static String        s_url, s_tok, s_id;
static volatile bool s_active = false;
static volatile bool s_openAp = false;         // scan open APs for one that can reach the relay
static volatile bool s_onOpen = false;         // currently connected via an open AP (vs saved creds)
static volatile int  s_state  = 0;             // 0 off · 1 connecting · 2 online(creds) · 3 scanning · 4 online(open AP)
static TaskHandle_t  s_task   = nullptr;
static QueueHandle_t s_cmdQ   = nullptr;       // task -> main loop  (pulled commands)
static QueueHandle_t s_replyQ = nullptr;       // main loop -> task  (replies to POST)
struct RelayMsg { char s[560]; };              // PoC commands can be long (file-write chunks / scripts)

static bool isHttps() { return s_url.startsWith("https"); }
// Normalise: strip trailing '/', and lowercase the scheme (mobile keyboards auto-capitalise
// the first letter -> "Https://", which would otherwise be treated as plain http).
static void normUrl(String& u) {
  u.trim(); while (u.endsWith("/")) u.remove(u.length() - 1);
  int p = u.indexOf("://"); if (p > 0) { String s = u.substring(0, p); s.toLowerCase(); u = s + u.substring(p); }
}

static void computeId() {
  // Custom id (config) wins. Else the factory-burned base MAC — STABLE across boots (and available
  // before BLE init). The BLE address can be a rotating/resolvable private address, which would
  // change our mailbox id every boot, so the portal's saved id goes stale on an auto-boot (board
  // polls one id, portal talks to another).
  s_pref.begin("relay", true); String cid = s_pref.getString("id", ""); s_pref.end();
  cid.trim(); cid.replace(" ", ""); cid.replace("/", "");
  if (cid.length()) { s_id = cid; return; }
  char b[16]; snprintf(b, sizeof(b), "bt-%06x", (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF));
  s_id = b;
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
bool   relayGetOpenAp()      { s_pref.begin("relay", true); bool o = s_pref.getBool("openap", false); s_pref.end(); return o; }
void   relaySetOpenAp(bool on) { s_pref.begin("relay", false); s_pref.putBool("openap", on); s_pref.end(); }
String relayGetId()          { s_pref.begin("relay", true); String v = s_pref.getString("id", ""); s_pref.end(); return v; }
void   relaySetId(const String& id) { s_pref.begin("relay", false); s_pref.putString("id", id); s_pref.end(); s_id = ""; computeId(); }  // recompute now
// Keep BLE up alongside WiFi+TLS? The S3 has enough SRAM even without PSRAM (headless verified);
// the C5 only fits it with PSRAM (the no-PSRAM Waveshare must drop BLE — SSL alloc -32512).
bool relayChipCanCoexist() {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  return true;
#else
  return ESP.getPsramSize() > 0;
#endif
}

// ---- HTTP — ONE persistent keep-alive connection, reused across pull/post so we don't pay
// a fresh TLS handshake every request (that was the dominant per-command latency). Only ever
// used from the relay task, so a single TLS context exists at a time. ----
static const char SEP = '\x1e';                  // batch separator (relay joins queued items with it)
static WiFiClientSecure s_tls;
static WiFiClient       s_plain;
static HTTPClient       s_http;
static bool             s_tlsReady = false;

// One request on the persistent connection. postBody != nullptr -> POST, else a (long-poll) GET.
static int httpDo(const String& u, const char* postBody, String* out) {
  if (!s_tlsReady) { s_tls.setInsecure(); s_tlsReady = true; }
  s_http.setReuse(true);                          // keep the socket open across begin()/end()
  s_http.setTimeout(postBody ? 8000 : 30000);     // GET long-polls ~25s server-side
  bool ok = isHttps() ? s_http.begin(s_tls, u) : s_http.begin(s_plain, u);
  if (!ok) return 0;
  if (s_tok.length()) s_http.addHeader("x-relay-token", s_tok);
  int code;
  if (postBody) { s_http.addHeader("Content-Type", "text/plain"); code = s_http.POST((uint8_t*)postBody, strlen(postBody)); }
  else code = s_http.GET();
  if (code == 200 && out) *out = s_http.getString();
  s_http.end();                                   // with setReuse(true) this keeps the connection
  if (code <= 0) { s_tls.stop(); s_plain.stop(); }  // connect failed (e.g. a keep-alive socket left dead by a
                                                    // network switch) -> drop it so the next call handshakes fresh
  return code;
}
static String httpPull(int* code) { String out; int c = httpDo(s_url + "/pull/" + s_id, nullptr, &out); if (code) *code = c; return out; }
static bool   httpPostReplyOnce(const String& u, const char* line) { return httpDo(u, line, nullptr) == 200; }

// Reachability probe on a THROWAWAY TLS client — a captive portal's failed/hijacked handshake
// must not wedge the persistent s_tls we poll with. Returns true only for the relay's own "ok".
static bool probeHealth() {
  bool https = s_url.startsWith("https");
  WiFiClientSecure sec; WiFiClient plain;
  if (https) sec.setInsecure();
  HTTPClient h; h.setTimeout(8000);
  bool began = https ? h.begin(sec, s_url + "/health") : h.begin(plain, s_url + "/health");
  if (!began) return false;
  int code = h.GET(); String b = (code == 200) ? h.getString() : String();
  h.end();
  return code == 200 && b.startsWith("ok");
}

void relayPostReply(const char* line) {          // queue only; the task does the POST (one TLS at a time)
  if (!s_active || !s_replyQ) return;
  RelayMsg m; strlcpy(m.s, line, sizeof(m.s));
  xQueueSend(s_replyQ, &m, pdMS_TO_TICKS(4000));  // block if full — don't drop chunks of a burst (e.g. __PLGET__)
}

// Scan for OPEN APs and connect to the strongest one that can actually reach the relay (its
// /health returns "ok"). The body check rejects captive-portal APs (they connect but return a
// login page) and accepts a cold Render once it wakes. Returns true if we ended up online.
static bool relayTryOpenAps() {
  Serial.println("[relay] scanning for open APs…");
  int n = WiFi.scanNetworks();
  if (n <= 0) { WiFi.scanDelete(); s_onOpen = false; return false; }
  int order[24]; int m = 0;
  for (int i = 0; i < n && m < 24; i++) if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) order[m++] = i;
  for (int a = 0; a < m; a++) for (int b = a + 1; b < m; b++)                       // strongest RSSI first
    if (WiFi.RSSI(order[b]) > WiFi.RSSI(order[a])) { int t = order[a]; order[a] = order[b]; order[b] = t; }
  bool ok = false;
  for (int k = 0; k < m && !ok; k++) {
    String ssid = WiFi.SSID(order[k]);
    Serial.printf("[relay] trying open AP '%s' (%d dBm)\n", ssid.c_str(), WiFi.RSSI(order[k]));
    WiFi.begin(ssid.c_str());                                                       // open network, no password
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) vTaskDelay(pdMS_TO_TICKS(200));
    if (WiFi.status() == WL_CONNECTED) {
      if (probeHealth()) {
        Serial.printf("[relay] '%s' reaches the relay — using it\n", ssid.c_str()); ok = true; break; }
      Serial.printf("[relay] '%s' connected but no relay (captive/cold?) — next\n", ssid.c_str());
    }
    if (!ok) { WiFi.disconnect(true); dns_clear_cache(); }   // erase the rejected AP + flush the DNS it poisoned (captive portals resolve every host to their login IP)
  }
  WiFi.scanDelete();
  s_onOpen = ok;                                  // remember HOW we're online (open AP vs creds fallback)
  return ok;
}

static void relayTask(void*) {
  bool wasUp = false; uint32_t lastLog = 0;
  for (;;) {
    if (!s_active) { s_state = 0; wasUp = false; vTaskDelay(pdMS_TO_TICKS(400)); continue; }
    if (WiFi.status() != WL_CONNECTED) {
      wasUp = false;
      if (s_openAp) {
        s_state = 3;                                    // scanning/attempting -> STA blinks BLUE
        if (!relayTryOpenAps()) {
          if (netHasCreds()) {                          // no open AP reached the relay -> fall back to saved creds
            Serial.println("[relay] no open AP reachable — falling back to saved WiFi creds");
            WiFi.disconnect(true, true); WiFi.mode(WIFI_OFF); vTaskDelay(pdMS_TO_TICKS(300));  // flush captive DNS/lwip state
            netConnect();
            uint32_t t0 = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) vTaskDelay(pdMS_TO_TICKS(200));
          }
          if (WiFi.status() != WL_CONNECTED) vTaskDelay(pdMS_TO_TICKS(8000));   // still nothing — rescan
        }
      } else {
        s_state = 1;
        if (millis() - lastLog > 3000) { lastLog = millis(); Serial.printf("[relay] waiting for WiFi (status=%d)\n", WiFi.status()); }
        vTaskDelay(pdMS_TO_TICKS(500));
      }
      continue;
    }
    if (!wasUp) { wasUp = true;
      s_tls.stop(); s_plain.stop();                // a keep-alive socket from a previous network (open-AP scan / captive) is dead — start fresh
      dns_clear_cache();                           // flush any host->captive-IP entry left by a probed open AP
      // Confirm the relay is reachable with a FAST isolated GET /health so we show online immediately,
      // instead of sitting orange for the ~25s a first idle long-poll parks.
      bool reach = probeHealth();
      s_state = reach ? (s_onOpen ? 4 : 2) : 1;
      Serial.printf("[relay] WiFi up (%s), IP %s — relay %s — polling %s/pull/%s\n", s_onOpen ? "open AP" : "creds",
                    WiFi.localIP().toString().c_str(), reach ? "reachable" : "UNREACHABLE", s_url.c_str(), s_id.c_str()); }
    RelayMsg m;
    while (xQueueReceive(s_replyQ, &m, 0)) { for (int i = 0; i < 4; i++) { if (httpPostReplyOnce(s_url + "/reply/" + s_id, m.s)) break; vTaskDelay(pdMS_TO_TICKS(150)); } }
    int code = 0; String batch = httpPull(&code);  // may hold several commands joined by SEP
    // Keep green/blue while the relay answers (200 = commands, 204 = idle long-poll); a bad token (401)
    // or an unreachable relay (<=0) drops to orange so a dead link is visible.
    if (code == 200 || code == 204) s_state = s_onOpen ? 4 : 2;
    else { s_state = 1; if (millis() - lastLog > 4000) { lastLog = millis(); Serial.printf("[relay] pull got %d — relay not reachable (rssi=%d)\n", code, WiFi.RSSI()); } }
    if (batch.length()) {
      int start = 0;
      while (start < (int)batch.length()) {
        int sep = batch.indexOf(SEP, start);
        String one = (sep < 0) ? batch.substring(start) : batch.substring(start, sep);
        if (one.length()) { Serial.printf("[relay] cmd: %.40s\n", one.c_str());
          RelayMsg c; strlcpy(c.s, one.c_str(), sizeof(c.s)); xQueueSend(s_cmdQ, &c, pdMS_TO_TICKS(4000)); }
        if (sep < 0) break; start = sep + 1;
      }
      vTaskDelay(pdMS_TO_TICKS(20));                // let the main loop dispatch + produce replies before the next pull
    } else vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void relayBegin() {
  s_pref.begin("relay", true);
  s_url = s_pref.getString("url", ""); normUrl(s_url); s_tok = s_pref.getString("tok", "");
  bool a = s_pref.getBool("auto", false);
  bool openap = s_pref.getBool("openap", false);
  s_pref.end();
  if (a && s_url.length()) {
    if (openap) { Serial.println("[relay] auto-connect on boot (open-AP scan)"); relayGoOpenAp(); }
    else { Serial.println("[relay] auto-connect on boot"); relayGoRemote(s_url, s_tok); }
  }
}

void relaySaveCreds(const String& url, const String& token) {   // persist without connecting (autosave)
  s_url = url; normUrl(s_url);
  s_tok = token;
  s_pref.begin("relay", false); s_pref.putString("url", s_url); s_pref.putString("tok", s_tok); s_pref.end();
}

bool relayGoRemote(const String& url, const String& token) {
  relaySaveCreds(url, token);
  computeId();
  s_state = 1;
  Serial.printf("[relay] go remote: %s as %s — bringing up WiFi STA\n", s_url.c_str(), s_id.c_str());
  netConnect();                                  // STA up with the saved WiFi creds
  s_active = true; s_openAp = false; s_onOpen = false;
  if (!s_cmdQ)   s_cmdQ   = xQueueCreate(16, sizeof(RelayMsg));
  if (!s_replyQ) s_replyQ = xQueueCreate(16, sizeof(RelayMsg));
  if (!s_task)   xTaskCreatePinnedToCore(relayTask, "relay", 8192, nullptr, 1, &s_task, 0);
  return true;
}

// Go remote by SCANNING for an open AP that can reach the relay (falls back to saved creds).
bool relayGoOpenAp() {
  static uint32_t lastGo = 0;                    // debounce: a double-send / re-pulled cmd must not
  if (s_active && s_openAp && millis() - lastGo < 5000) return true;   // thrash an in-progress scan
  lastGo = millis();
  if (!s_url.length()) return false;
  computeId();
  s_state = 3; s_active = true; s_openAp = true; s_onOpen = false;
  Serial.println("[relay] go remote via open-AP scan");
  WiFi.mode(WIFI_STA);
  if (!s_cmdQ)   s_cmdQ   = xQueueCreate(16, sizeof(RelayMsg));
  if (!s_replyQ) s_replyQ = xQueueCreate(16, sizeof(RelayMsg));
  if (!s_task)   xTaskCreatePinnedToCore(relayTask, "relay", 8192, nullptr, 1, &s_task, 0);
  return true;
}

void relayStop() { s_active = false; s_state = 0; Serial.println("[relay] stopped"); }

void relayTick() {
  if (!s_cmdQ) return;
  RelayMsg m;
  while (xQueueReceive(s_cmdQ, &m, 0)) bleHidHandleExternal(m.s);
}
