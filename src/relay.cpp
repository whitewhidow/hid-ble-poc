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
// Normalise: strip trailing '/', and lowercase the scheme (mobile keyboards auto-capitalise
// the first letter -> "Https://", which would otherwise be treated as plain http).
static void normUrl(String& u) {
  u.trim(); while (u.endsWith("/")) u.remove(u.length() - 1);
  int p = u.indexOf("://"); if (p > 0) { String s = u.substring(0, p); s.toLowerCase(); u = s + u.substring(p); }
}

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
  return code;
}
static String httpPull() { String out; httpDo(s_url + "/pull/" + s_id, nullptr, &out); return out; }
static bool   httpPostReplyOnce(const String& u, const char* line) { return httpDo(u, line, nullptr) == 200; }

void relayPostReply(const char* line) {          // queue only; the task does the POST (one TLS at a time)
  if (!s_active || !s_replyQ) return;
  RelayMsg m; strlcpy(m.s, line, sizeof(m.s));
  xQueueSend(s_replyQ, &m, pdMS_TO_TICKS(4000));  // block if full — don't drop chunks of a burst (e.g. __PLGET__)
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
    String batch = httpPull();                     // may hold several commands joined by SEP
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
  s_pref.end();
  if (a && s_url.length()) { Serial.println("[relay] auto-connect on boot"); relayGoRemote(s_url, s_tok); }
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
  s_active = true;
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
