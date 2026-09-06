// Remote control over WiFi via a relay you host (see relay/). The board long-polls the
// relay for control commands and posts replies, running them through the SAME handler as
// the BLE control service. On PSRAM boards BLE-HID keeps working alongside; on no-PSRAM
// boards BLE is dropped to fit the TLS handshake (USB-HID still types). See relay.cpp.
#pragma once
#include <Arduino.h>

void        relayBegin();                                  // load saved settings; auto-connect if enabled
bool        relayGoRemote(const String& url, const String& token);  // save + STA up + start polling
void        relayStop();
bool        relayActive();
int         relayState();                                  // 0 off · 1 connecting · 2 online
const char* relayId();                                     // this board's mailbox id ("bt-<mac6>")
void        relayPostReply(const char* line);              // reply sink while running a relay command
void        relayTick();                                   // call from loop(): dispatch pulled commands

String      relayGetUrl();                                 // for the portal to show/pre-fill
String      relayGetToken();
bool        relayGetAuto();                                // connect-on-boot
void        relaySetAuto(bool on);
