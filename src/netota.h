// WiFi station + in-app OTA self-update (T-Embed only; stubbed on the C5).
// Creds are provisioned over BLE from the phone page and stored in NVS; the board
// then pulls the app image from POC_OTA_URL and writes it to the spare OTA slot.
#pragma once
#include <Arduino.h>

void        netBegin();                                   // load creds, start WiFi if set (non-blocking)
bool        netHasCreds();
void        netSetCreds(const String& ssid, const String& pass);  // save + (re)connect
void        netClearCreds();
String      netStatus();                                  // "wifi:<ssid>|<state>|<ip>|<ver>"
const char* netVersion();

// Run OTA now (blocking, ~seconds). cb(pct,msg) reports progress/errors; returns
// "ok" on success (caller reboots) or "err:<what>".
String      netOtaUpdate(void (*cb)(int pct, const char* msg));
