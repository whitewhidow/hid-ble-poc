// See netota.h.
#include "netota.h"
#include "version.h"

const char* netVersion() { return POC_VERSION; }

#if defined(POC_BOARD_TEMBED) || defined(POC_BOARD_TDONGLE) || defined(POC_BOARD_HEADLESS) || defined(POC_BOARD_CARDPUTER)
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Preferences.h>

static Preferences s_prefs;
static String      s_ssid, s_pass;

static void loadCreds() {
    s_prefs.begin("poc", true);
    s_ssid = s_prefs.getString("ssid", "");
    s_pass = s_prefs.getString("pass", "");
    s_prefs.end();
}

void netBegin() { loadCreds(); }        // load creds only — NO auto-connect; the web triggers it
bool netHasCreds() { return s_ssid.length() > 0; }

void netConnect() {                     // explicit connect with the saved creds
    if (!s_ssid.length()) return;
    WiFi.mode(WIFI_STA); WiFi.setSleep(true);
    WiFi.disconnect();
    WiFi.begin(s_ssid.c_str(), s_pass.c_str());
}

void netSetCreds(const String& ssid, const String& pass) {
    s_prefs.begin("poc", false);
    s_prefs.putString("ssid", ssid); s_prefs.putString("pass", pass);
    s_prefs.end();
    s_ssid = ssid; s_pass = pass;       // save only — connect is a separate, explicit web action
}
void netClearCreds() {
    s_prefs.begin("poc", false); s_prefs.remove("ssid"); s_prefs.remove("pass"); s_prefs.end();
    s_ssid = ""; s_pass = ""; WiFi.disconnect(true);
}

String netStatus() {
    wl_status_t w = WiFi.status();
    const char* st = (w == WL_CONNECTED) ? "connected" : (s_ssid.length() ? "connecting" : "unset");
    String ip = (w == WL_CONNECTED) ? WiFi.localIP().toString() : String("-");
    return String("wifi:") + (s_ssid.length() ? s_ssid : String("-")) + "|" + st + "|" + ip + "|" + POC_VERSION;
}

String netOtaUpdate(void (*cb)(int, const char*), const char* url) {
    if (WiFi.status() != WL_CONNECTED) { if (cb) cb(0, "wifi not connected"); return "err:wifi"; }
    if (cb) cb(0, "connecting to github");

    WiFiClientSecure client; client.setInsecure();      // GitHub certs are valid; skip the bundle
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);   // release asset 302s to a CDN
    http.setTimeout(15000);
    if (!http.begin(client, url)) { if (cb) cb(0, "http begin fail"); return "err:begin"; }

    int code = http.GET();
    if (code != HTTP_CODE_OK) { char e[24]; snprintf(e, sizeof(e), "http %d", code); if (cb) cb(0, e); http.end(); return "err:http"; }
    int total = http.getSize();
    if (total <= 0) { if (cb) cb(0, "no content-length"); http.end(); return "err:size"; }

    if (!Update.begin((size_t)total)) { if (cb) cb(0, Update.errorString()); http.end(); return "err:begin2"; }
    if (cb) Update.onProgress([cb](size_t d, size_t t) {
        if (t) { char m[24]; int p = (int)(d * 100 / t); snprintf(m, sizeof(m), "writing %d%%", p); cb(p, m); }
    });

    WiFiClient* stream = http.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    http.end();
    if (written != (size_t)total) { Update.abort(); if (cb) cb(0, "incomplete download"); return "err:write"; }
    if (!Update.end(true))        { if (cb) cb(0, Update.errorString()); return "err:end"; }
    if (cb) cb(100, "done, rebooting");
    return "ok";
}

#else   // ---- C5 / no OTA (4MB can't fit A/B slots) --------------------------
void   netBegin() {}
bool   netHasCreds() { return false; }
void   netConnect() {}
void   netSetCreds(const String&, const String&) {}
void   netClearCreds() {}
String netStatus() { return String("wifi:unsupported|-|-|") + POC_VERSION; }
String netOtaUpdate(void (*cb)(int, const char*), const char*) { if (cb) cb(0, "OTA not on this board"); return "err:unsupported"; }
#endif
