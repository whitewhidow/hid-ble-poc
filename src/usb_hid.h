#pragma once
enum PocOS { POC_OS_UNKNOWN = 0, POC_OS_WINDOWS, POC_OS_LINUX, POC_OS_MACOS,
             POC_OS_IOS, POC_OS_ANDROID, POC_OS_CHROMEOS };

void usbHidBegin();     // real on POC_HAS_USB_HID (S3); no-op stub on the C5

#ifdef POC_HAS_USB_HID
int         usbDetectOS();
const char* usbOsName(int os);
void        usbHidType(const char* s);
void        usbSamplePayload(int os);   // runs the OS's payload file from LittleFS
#endif
