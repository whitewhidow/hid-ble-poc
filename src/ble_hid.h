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
