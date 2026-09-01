#include "usb_hid.h"
#include "ble_hid.h"   // bleHidMac() for {MAC} substitution
#include <Arduino.h>

#ifndef POC_HAS_USB_HID
// ---- C5 / no-USB-OTG: stub. The USB-side bootstrap is run manually on the PC. --
void usbHidBegin() { /* no USB HID on this chip */ }
// No LittleFS payloads on this board -> editing is unavailable.
bool pocFsRead(const char*, String&)        { return false; }
bool pocFsWriteBegin(const char*)           { return false; }
bool pocFsWriteChunk(const uint8_t*, size_t){ return false; }
bool pocFsWriteEnd(size_t&)                 { return false; }

#else
// ---- S3 / real USB HID: OS detection + LittleFS payload interpreter -----------
#include <LittleFS.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
// Stock header lacks these; press() maps KEY_x-0x88 -> HID usage.
#ifndef KEY_NUM_LOCK
#define KEY_NUM_LOCK    0xDB
#endif
#ifndef KEY_SCROLL_LOCK
#define KEY_SCROLL_LOCK 0xCF
#endif

static USBHIDKeyboard Keyboard;

static volatile bool          led_response_received = false;
static volatile int           led_event_count = 0;
static volatile unsigned long led_event_time = 0;
static volatile bool          caps_status = false, num_status = false, scroll_status = false;
static unsigned long caps_sent_time = 0, num_sent_time = 0, scroll_sent_time = 0;
static unsigned long caps_delay = 0, num_delay = 0, scroll_delay = 0;

static void usbEventCallback(void*, esp_event_base_t base, int32_t id, void* ev) {
    if (base == ARDUINO_USB_HID_KEYBOARD_EVENTS && id == ARDUINO_USB_HID_KEYBOARD_LED_EVENT) {
        arduino_usb_hid_keyboard_event_data_t* d = (arduino_usb_hid_keyboard_event_data_t*)ev;
        led_response_received = true; led_event_count++; led_event_time = millis();
        caps_status = d->capslock != 0; num_status = d->numlock != 0; scroll_status = d->scrolllock != 0;
        if (caps_sent_time   > 0 && caps_delay   == 0) caps_delay   = led_event_time - caps_sent_time;
        if (num_sent_time    > 0 && num_delay    == 0) num_delay    = led_event_time - num_sent_time;
        if (scroll_sent_time > 0 && scroll_delay == 0) scroll_delay = led_event_time - scroll_sent_time;
    }
}

static void toggleKey(uint8_t key, unsigned long* t) {
    *t = millis(); led_response_received = false;
    Keyboard.press(key); delay(300); Keyboard.release(key); delay(800);
}

static int classifyOS() {
    led_event_count = 0; caps_status = num_status = scroll_status = false;
    caps_delay = num_delay = scroll_delay = 0; caps_sent_time = num_sent_time = scroll_sent_time = 0;
    led_response_received = false;
    uint8_t initial_caps = caps_status;
    toggleKey(KEY_CAPS_LOCK, &caps_sent_time); delay(1500);
    if (!led_response_received && caps_status != initial_caps) return POC_OS_IOS;
    toggleKey(KEY_NUM_LOCK, &num_sent_time); delay(1200);
    toggleKey(KEY_SCROLL_LOCK, &scroll_sent_time); delay(1200);
    if (led_event_count == 0) return (caps_status != initial_caps) ? POC_OS_IOS : POC_OS_MACOS;
    if (led_event_count >= 3 && caps_delay < 100 && num_delay < 100 && scroll_delay < 100) return POC_OS_WINDOWS;
    bool hn = (num_delay > 0), hs = (scroll_delay > 0);
    if (led_event_count == 1 && caps_status != initial_caps && caps_delay > 0 && caps_delay < 20 && !hn && !hs) return POC_OS_CHROMEOS;
    if (num_status && !scroll_status && hn && !hs) return POC_OS_LINUX;
    if ((caps_delay > 200 || num_delay > 200 || scroll_delay > 200) && (hn || hs)) return POC_OS_ANDROID;
    if (caps_status != initial_caps) return POC_OS_IOS;
    return POC_OS_UNKNOWN;
}

static void resetLocks() {
    unsigned long tmp; delay(300);
    if (caps_status)   { toggleKey(KEY_CAPS_LOCK,   &tmp); delay(200); }
    if (num_status)    { toggleKey(KEY_NUM_LOCK,    &tmp); delay(200); }
    if (scroll_status) { toggleKey(KEY_SCROLL_LOCK, &tmp); delay(200); }
}

int usbDetectOS() { int os = classifyOS(); resetLocks(); return os; }

const char* usbOsName(int os) {
    switch (os) {
        case POC_OS_WINDOWS: return "Windows"; case POC_OS_LINUX: return "Linux";
        case POC_OS_MACOS: return "macOS";     case POC_OS_IOS: return "iOS";
        case POC_OS_ANDROID: return "Android"; case POC_OS_CHROMEOS: return "ChromeOS";
        default: return "Unknown";
    }
}

void usbHidType(const char* s) { for (const char* p = s; *p; ++p) { Keyboard.write((uint8_t)*p); delay(8); } }

static void tapGui(const String& arg) {
    Keyboard.press(KEY_LEFT_GUI);
    if (arg == "SPACE") Keyboard.press(' ');
    else if (arg.length() == 1) Keyboard.press(arg[0]);
    delay(90); Keyboard.releaseAll();
}

// Our own tiny interpreter: GUI [key] | STRING <text> | ENTER | DELAY <ms> | # comment
static void usbRunPayloadFile(const char* path) {
    File f = LittleFS.open(path, "r");
    if (!f) { usbHidType("(payload file missing: "); usbHidType(path); usbHidType(")\n"); return; }
    while (f.available()) {
        String line = f.readStringUntil('\n'); line.trim();
        if (line.length() == 0 || line[0] == '#') continue;
        if (line == "ENTER") Keyboard.write((uint8_t)'\n');
        else if (line.startsWith("DELAY ")) delay(line.substring(6).toInt());
        else if (line.startsWith("STRING ")) { String s = line.substring(7); s.replace("{MAC}", bleHidMac()); usbHidType(s.c_str()); }
        else if (line == "GUI") tapGui("");
        else if (line.startsWith("GUI ")) { String a = line.substring(4); a.trim(); tapGui(a); }
        else if (line.startsWith("CTRLALT ")) {   // Ctrl+Alt+<key>, e.g. CTRLALT t (open terminal)
            char k = line.charAt(8);
            Keyboard.press(KEY_LEFT_CTRL); Keyboard.press(KEY_LEFT_ALT); Keyboard.press(k);
            delay(90); Keyboard.releaseAll();
        }
    }
    f.close();
}

void usbSamplePayload(int os) {
    const char* path;
    switch (os) {
        case POC_OS_WINDOWS: path = "/windows.txt"; break;
        case POC_OS_MACOS:   path = "/macos.txt";   break;
        case POC_OS_LINUX:
        default:             path = "/linux.txt";   break;   // fall back to Linux (our target) when detection is unsure
    }
    usbRunPayloadFile(path);
}

// ---- Payload-file editing over BLE (LittleFS) --------------------------------
// Only the three OS payloads are addressable; reject anything else so a bad/rogue
// name can't reach arbitrary paths.
static bool fsPath(const char* os, char* out, size_t n) {
    if (!strcmp(os, "linux") || !strcmp(os, "windows") || !strcmp(os, "macos")) {
        snprintf(out, n, "/%s.txt", os); return true;
    }
    return false;
}

static File   s_wf;          // file open for a phone-driven write
static size_t s_wn = 0;      // bytes written so far

bool pocFsRead(const char* os, String& out) {
    char p[24]; if (!fsPath(os, p, sizeof(p))) return false;
    LittleFS.begin(true);
    File f = LittleFS.open(p, "r"); if (!f) return false;
    out = ""; out.reserve(f.size());
    while (f.available()) out += (char)f.read();
    f.close(); return true;
}
bool pocFsWriteBegin(const char* os) {
    char p[24]; if (!fsPath(os, p, sizeof(p))) return false;
    LittleFS.begin(true);
    s_wf = LittleFS.open(p, "w"); s_wn = 0; return (bool)s_wf;
}
bool pocFsWriteChunk(const uint8_t* data, size_t n) {
    if (!s_wf) return false;
    size_t w = s_wf.write(data, n); s_wn += w; return w == n;
}
bool pocFsWriteEnd(size_t& sizeOut) {
    if (!s_wf) return false;
    sizeOut = s_wn; s_wf.close(); return true;
}

void usbHidBegin() {
    LittleFS.begin(true);
    USB.onEvent(usbEventCallback);
    Keyboard.onEvent(usbEventCallback);
    USB.begin();
    Keyboard.begin();
}
#endif // POC_HAS_USB_HID
