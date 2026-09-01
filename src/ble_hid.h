#pragma once
void bleHidBegin();
bool bleHidConnected();   // PC HID link live
bool bleHidPhone();       // phone/web on the control service
void bleHidType(const char* s);   // types into the paired host (Enter as '\n')
void bleHidTick();
const char* bleHidMac();          // the board's own BLE address, "aa:bb:cc:dd:ee:ff"
int  bleHidConnCount();           // total BLE links (PCs + phone)
void bleHidDropAll();             // disconnect every current link, re-advertise
bool bleAutorun();                // runtime AUTORUN setting (persisted in NVS)
void bleSetAutorun(bool on);      // set + persist AUTORUN
int  bleTargetOs();               // default menu index to arm (0=Autodetect..3=macOS), NVS
void bleSetTargetOs(int idx);     // set + persist the target OS
bool bleArmBoot();                // auto-arm the target OS at boot (NVS; default: on for dongle)
void bleSetArmBoot(bool on);      // set + persist arm-at-boot
int  bleFireDelay();              // ms to wait after a plug before firing (NVS; default 2000)
void bleSetFireDelay(int ms);     // set + persist the fire delay
