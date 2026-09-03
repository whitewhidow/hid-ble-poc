#pragma once
#include <Arduino.h>
enum PocOS { POC_OS_UNKNOWN = 0, POC_OS_WINDOWS, POC_OS_LINUX, POC_OS_MACOS,
             POC_OS_IOS, POC_OS_ANDROID, POC_OS_CHROMEOS };

void usbHidBegin();     // real on POC_HAS_USB_HID (S3); no-op stub on the C5
bool usbHidMounted();   // true when the board's USB is enumerated on a live host (false on C5)

// Editing the on-FS payload files over BLE (the phone web page). Real on
// POC_HAS_USB_HID (LittleFS present); stubbed to false on the C5 (no FS).
// `os` is one of "linux" / "windows" / "macos" -> /<os>.txt.
bool pocFsRead(const char* os, String& out);          // read whole file into out
bool pocFsWriteBegin(const char* os);                 // open for write (truncate)
bool pocFsWriteChunk(const uint8_t* data, size_t n);  // append bytes to the open file
bool pocFsWriteEnd(size_t& sizeOut);                  // close; sizeOut = bytes written

// Payload library (arbitrary named payloads) + which one is loaded into each OS
// slot. Real on POC_HAS_USB_HID (LittleFS); stubbed to empty/false on the C5.
String pocLibList();                                  // '\n'-joined payload names
bool   pocLibRead(const char* name, String& out);     // read a library payload
bool   pocLibWriteBegin(const char* name);            // open a library payload for write, then pocFsWriteChunk/End
bool   pocLibDelete(const char* name);                // delete a library payload
bool   pocLibLoadToSlot(const char* name, const char* os);  // copy lib payload -> the OS slot file
String pocSlotAssignments();                          // "linux=<n>;windows=<n>;macos=<n>"

#ifdef POC_HAS_USB_HID
int         usbDetectOS();
const char* usbOsName(int os);
void        usbHidType(const char* s);
void        usbHidKey(const char* shortName);        // one nav/special key over USB
void        usbHidChord(const char* mods, char ch);  // modifier(c/a/g)+char chord over USB
void        usbRunScript(const char* s);             // run an Evil-Crow script string over USB
void        usbConsumer(uint16_t usage);             // media / consumer-control key over USB
void        usbSysCtl(uint8_t code);                 // system control: 1=power 2=sleep 3=wake
void        usbMouseMove(int dx, int dy, int wheel); // relative mouse move + wheel
void        usbMouseClick(uint8_t buttons);          // mouse click (MOUSE_LEFT/RIGHT/MIDDLE bits)
void        usbMousePress(uint8_t buttons);
void        usbMouseRelease(uint8_t buttons);
void        usbSamplePayload(int os);   // runs the OS's payload file from LittleFS
#endif
