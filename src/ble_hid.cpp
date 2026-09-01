// Shared BLE side (NimBLE 2.x): a HID keyboard (-> the PC) PLUS a small custom
// control service (-> the phone web page). The board holds BOTH links at once:
// the phone writes text to the control service, and the board injects it into the
// PC over BLE-HID. Advertises as "PoC-KBD"; bonds Just Works.
#include "ble_hid.h"
#include "usb_hid.h"   // pocFsRead / pocFsWrite* — edit the payload files over BLE
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
static char          g_pending[256] = "";        // text from the phone, typed by the loop
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
    }
}

// Runs in the main loop (safe context, not a BLE callback): handle queued work.
void bleHidTick() {
    if (g_cmdReq) { g_cmdReq = false; handleCmd(g_cmd); }
    if (g_hasPending) { g_hasPending = false; bleHidType(g_pending); }
}
