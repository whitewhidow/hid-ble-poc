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

#ifdef POC_HAS_USB_HID
int         usbDetectOS();
const char* usbOsName(int os);
void        usbHidType(const char* s);
void        usbSamplePayload(int os);   // runs the OS's payload file from LittleFS
#endif
