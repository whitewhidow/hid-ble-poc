#include "display.h"
#include "version.h"

#if defined(POC_BOARD_HEADLESS)
// Headless board (no panel): the BLE portal is the entire UI, so every display
// call is a no-op — and we never init a panel (which would hang with none wired).
void dispBegin() {}
void dispShow(const char*, const char*, uint32_t) {}
void dispCenter(const char*, const char*, uint32_t) {}
void dispSplash(const char*, const char*) {}
void dispBle(bool, bool, bool, bool, bool, int, int, int) {}
void dispOff() {}
void dispOn() {}
void dispMenu(const char*, const char* const*, int, int) {}
void dispCmd(const char*, const char*) {}
#else

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// Board-specific ST7789 pins/geometry (from BBoink board.h), selected by flag.
#if defined(POC_BOARD_TEMBED)
  #define PIN_SCLK 11
  #define PIN_MOSI 9
  #define PIN_MISO 10
  #define PIN_CS   41
  #define PIN_DC   16
  #define PIN_RST  40
  #define PIN_BL   21
  #define PANEL_W  170
  #define PANEL_H  320
  #define OFFX     35
  #define OFFY     0
  #define PANEL_FREQ 40000000
#elif defined(POC_BOARD_TDONGLE)
  // LilyGo T-Dongle S3 — ST7735S 80x160 LCD. Pins per LilyGo T-Dongle-S3
  // pin_config.h. UNVERIFIED on hardware; confirm/adjust on first boot (esp. the
  // backlight polarity and the panel offsets/rotation on the small 80x160 screen).
  #define PIN_SCLK 5
  #define PIN_MOSI 3
  #define PIN_MISO -1
  #define PIN_CS   4
  #define PIN_DC   2
  #define PIN_RST  1
  #define PIN_BL   38
  #define PANEL_W  80
  #define PANEL_H  160
  #define OFFX     26
  #define OFFY     1
  #define PANEL_FREQ 27000000
  #define PANEL_ST7735
#elif defined(POC_BOARD_CARDPUTER)
  // M5Cardputer ADV — StampS3, ST7789 135x240 (landscape 240x135). Panel on SPI3.
  #define PIN_SCLK 36
  #define PIN_MOSI 35
  #define PIN_MISO -1
  #define PIN_CS   37
  #define PIN_DC   34
  #define PIN_RST  33
  #define PIN_BL   38
  #define PANEL_W  135
  #define PANEL_H  240
  #define OFFX     52
  #define OFFY     40
  #define PANEL_FREQ 40000000
  #define PANEL_SPI3
#else
  #error "define POC_BOARD_TEMBED, POC_BOARD_TDONGLE or POC_BOARD_CARDPUTER"
#endif

// Most boards wire the panel to SPI2_HOST; the Cardputer uses SPI3_HOST.
#if defined(PANEL_SPI3)
  #define PANEL_SPI_HOST SPI3_HOST
#else
  #define PANEL_SPI_HOST SPI2_HOST
#endif

#if defined(PANEL_ST7735)
  typedef lgfx::Panel_ST7735S PocPanel;
#else
  typedef lgfx::Panel_ST7789  PocPanel;
#endif

// Per-board UI metrics. Landscape usable area: T-Embed ~320x170, T-Dongle ~160x80,
// so the T-Dongle needs ~half the font sizes and tighter positions or rows fall off.
#if defined(POC_BOARD_TDONGLE)
  #define UI_TS_TITLE 2      // title/header text size
  #define UI_TS_BODY  1      // body / menu-row text size
  #define UI_PAD      4      // left padding / marker x
  #define UI_TITLE_Y  3      // title baseline y
  #define UI_BODY_Y   22     // body start y (dispShow)
  #define UI_MENU_Y0  18     // first menu row y (5 rows must clear the status bar)
  #define UI_MENU_DY  9      // menu row spacing
  #define UI_MENU_LX  14     // menu label x (after the > marker)
  #define UI_BAR_TS   1      // status-bar text size
  #define UI_BAR_H    12     // status-bar reserved height
  #define UI_CTR_TY   4      // dispCenter title y
  #define UI_CTR_BY   24     // dispCenter body start y
  #define UI_CTR_DY   11     // dispCenter line spacing
#elif defined(POC_BOARD_CARDPUTER)
  // Cardputer landscape ~240x135 — between the T-Dongle and T-Embed; all 6 menu
  // rows must clear the status bar within 135px tall.
  #define UI_TS_TITLE 2
  #define UI_TS_BODY  1
  #define UI_PAD      6
  #define UI_TITLE_Y  4
  #define UI_BODY_Y   30
  #define UI_MENU_Y0  26
  #define UI_MENU_DY  15
  #define UI_MENU_LX  18
  #define UI_BAR_TS   1
  #define UI_BAR_H    14
  #define UI_CTR_TY   6
  #define UI_CTR_BY   34
  #define UI_CTR_DY   14
#else  // POC_BOARD_TEMBED
  #define UI_TS_TITLE 3
  #define UI_TS_BODY  2
  #define UI_PAD      8
  #define UI_TITLE_Y  8
  #define UI_BODY_Y   48
  #define UI_MENU_Y0  32     // 6 rows (incl. Custom) must clear the status bar
  #define UI_MENU_DY  18
  #define UI_MENU_LX  26
  #define UI_BAR_TS   2
  #define UI_BAR_H    24
  #define UI_CTR_TY   12
  #define UI_CTR_BY   60
  #define UI_CTR_DY   22
#endif

class LGFX_Poc : public lgfx::LGFX_Device {
    PocPanel      _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Light_PWM _light;
public:
    LGFX_Poc() {
        { auto c = _bus.config();
          c.spi_host = PANEL_SPI_HOST; c.spi_mode = 0; c.freq_write = PANEL_FREQ; c.freq_read = 16000000;
          c.pin_sclk = PIN_SCLK; c.pin_mosi = PIN_MOSI; c.pin_miso = PIN_MISO; c.pin_dc = PIN_DC;
          _bus.config(c); _panel.setBus(&_bus); }
        { auto c = _panel.config();
          c.pin_cs = PIN_CS; c.pin_rst = PIN_RST; c.pin_busy = -1;
          c.panel_width = PANEL_W; c.panel_height = PANEL_H; c.offset_x = OFFX; c.offset_y = OFFY; c.offset_rotation = 0;
          c.readable = false; c.invert = true; c.rgb_order = false;
#if defined(POC_BOARD_CARDPUTER)
          c.bus_shared = false;   // Cardputer panel is on a DEDICATED SPI3 bus (matches the
                                  // working boilerplate); bus_shared=true here leaves it black.
#else
          c.bus_shared = true;    // T-Embed/T-Dongle share the panel SPI with CC1101/SD/etc.
#endif
          _panel.config(c); }
        { auto c = _light.config(); c.pin_bl = PIN_BL; c.invert = false; c.freq = 44100; c.pwm_channel = 7;
          _light.config(c); _panel.setLight(&_light); }
        setPanel(&_panel);
    }
};

static LGFX_Poc lcd;

void dispBegin() {
    lcd.init();
#if defined(POC_BOARD_TDONGLE) || defined(POC_BOARD_CARDPUTER)
    lcd.setRotation(1);              // landscape
#else
    lcd.setRotation(3);              // T-Embed: landscape, flipped 180
#endif
    lcd.setBrightness(200);
    lcd.fillScreen(0x000000u);
}

void dispOff() { Serial.println("[disp] OFF"); lcd.setBrightness(0); lcd.sleep(); }
void dispOn()  { Serial.println("[disp] ON");  lcd.wakeup(); lcd.setBrightness(200); }

// Selectable list. Each row is drawn at a FIXED x so nothing shifts as the
// selection moves; the selected row is cyan (with a ">" marker), the rest gray.
void dispMenu(const char* title, const char* const* items, int count, int sel) {
    Serial.printf("[disp] menu sel=%d\n", sel);
    lcd.fillScreen(0x000000u);
    lcd.setTextWrap(false);
    lcd.setTextColor(lcd.color888(0x22, 0xD3, 0xE0), 0x000000u);
    lcd.setTextSize(UI_TS_TITLE); lcd.setCursor(UI_PAD, UI_TITLE_Y); lcd.print(title);
    lcd.setTextSize(UI_TS_BODY);
    for (int i = 0; i < count; i++) {
        int  y = UI_MENU_Y0 + i * UI_MENU_DY;
        bool s = (i == sel);
        lcd.setTextColor(s ? lcd.color888(0x22, 0xD3, 0xE0) : lcd.color888(0x8A, 0x97, 0xA2), 0x000000u);
        lcd.setCursor(UI_PAD,     y); lcd.print(s ? ">" : " ");
        lcd.setCursor(UI_MENU_LX, y); lcd.print(items[i]);      // label always at the same x
    }
}

void dispShow(const char* header, const char* body, uint32_t color) {
    Serial.printf("[disp] show '%s'\n", header);
    lcd.fillScreen(0x000000u);
    lcd.setTextWrap(true);
    lcd.setTextColor(lcd.color888((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF), 0x000000u);
    lcd.setTextSize(UI_TS_TITLE); lcd.setCursor(UI_PAD, UI_TITLE_Y);  lcd.print(header);
    lcd.setTextColor(lcd.color888(0xC8, 0xD2, 0xDA), 0x000000u);
    lcd.setTextSize(UI_TS_BODY); lcd.setCursor(UI_PAD, UI_BODY_Y); lcd.print(body);
}

// Centered header + (multi-line) body — used for the OTA screen.
void dispCenter(const char* header, const char* body, uint32_t color) {
    lcd.fillScreen(0x000000u);
    lcd.setTextWrap(false);
    int W = lcd.width();
    lcd.setTextColor(lcd.color888((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF), 0x000000u);
    lcd.setTextSize(UI_TS_TITLE);
    { int tw = lcd.textWidth(header); int x = (W - tw) / 2; if (x < 0) x = 0; lcd.setCursor(x, UI_CTR_TY); lcd.print(header); }
    lcd.setTextColor(lcd.color888(0xC8, 0xD2, 0xDA), 0x000000u);
    lcd.setTextSize(UI_TS_BODY);
    String b = body; int start = 0, y = UI_CTR_BY;
    while (true) {
        int nl = b.indexOf('\n', start);
        String ln = (nl < 0) ? b.substring(start) : b.substring(start, nl);
        int tw = lcd.textWidth(ln.c_str()); int x = (W - tw) / 2; if (x < 0) x = 0;
        lcd.setCursor(x, y); lcd.print(ln);
        y += UI_CTR_DY;
        if (nl < 0) break;
        start = nl + 1;
    }
}

// Long command at small font (wraps to fit), for showing the one-line payload.
void dispCmd(const char* header, const char* cmd) {
    lcd.fillScreen(0x000000u);
    lcd.setTextWrap(true);
    lcd.setTextColor(lcd.color888(0x22, 0xD3, 0xE0), 0x000000u);
    lcd.setTextSize(UI_TS_BODY); lcd.setCursor(UI_PAD, UI_TITLE_Y); lcd.print(header);
    lcd.setTextColor(lcd.color888(0xC8, 0xD2, 0xDA), 0x000000u);
    lcd.setTextSize(1); lcd.setCursor(UI_PAD, UI_BODY_Y); lcd.print(cmd);
}

// Boot splash — a graphical intro (scales to the panel): a little decorative key
// row, the title with a drop shadow, a subtitle, an accent underline, and the
// version/board footer. Replaces the old plain-text splash.
void dispSplash(const char* version, const char* board) {
    const int W = lcd.width(), H = lcd.height();
    const bool big = W >= 240;
    uint32_t cyan = lcd.color888(0x22, 0xD3, 0xE0), mag = lcd.color888(0xE8, 0x79, 0xF9);
    uint32_t dim  = lcd.color888(0x5a, 0x67, 0x72), dark = lcd.color888(0x0b, 0x2a, 0x2e);
    lcd.fillScreen(0x000000u);
    for (int y = 0; y < H; y += 4) lcd.drawFastHLine(0, y, W, lcd.color888(0x0a, 0x12, 0x16)); // faint scanlines
    // decorative keyboard band (a row of little keys), one cyan accent key
    int kw = big ? 18 : 10, kh = big ? 14 : 8, gap = big ? 5 : 3;
    int kn = (W - 16) / (kw + gap); if (kn > 12) kn = 12; if (kn < 1) kn = 1;
    int startx = (W - (kn * (kw + gap) - gap)) / 2, ky = (int)(H * 0.16);
    for (int i = 0; i < kn; i++)
        lcd.fillRoundRect(startx + i * (kw + gap), ky, kw, kh, 2, (i % 5 == 2) ? cyan : dark);
    // title with drop shadow
    lcd.setTextDatum(middle_center);
    lcd.setTextSize(big ? 5 : 3);
    lcd.setTextColor(dark); lcd.drawString("PoC-KBD", W / 2 + 2, H / 2 + 2);
    lcd.setTextColor(cyan); lcd.drawString("PoC-KBD", W / 2, H / 2);
    // subtitle + accent underline
    lcd.setTextSize(big ? 2 : 1);
    lcd.setTextColor(mag); lcd.drawString("USB >> BLE HID", W / 2, H / 2 + (big ? 30 : 14));
    if (big) { int uw = (int)(W * 0.5); lcd.fillRect((W - uw) / 2, H / 2 + 46, uw, 2, cyan); } // underline: big screens only (no room on the 80px T-Dongle)
    // footer: version + board
    lcd.setTextSize(1); lcd.setTextColor(dim);
    char f[48]; snprintf(f, sizeof(f), "v%s  -  %s", version, board);
    lcd.setTextDatum(bottom_center); lcd.drawString(f, W / 2, H - 3);
    // companion page URL (fits on one line on the wide boards; skip the 80px dongle)
    if (big) { lcd.setTextColor(cyan); lcd.drawString(POC_PAGE_URL, W / 2, H - 16); }
    lcd.setTextDatum(top_left);
}

// Persistent bottom status bar: PC / PHONE / USB-host as green (up) or red (down),
// plus the total connection count.
void dispBle(bool pc, bool phone, bool usb, bool autorun, bool armboot, int targetOs, int batt, int total) {
    int y = lcd.height() - UI_BAR_H;
    // NB: color888() returns 24-bit RGB888 (uint32_t). Keep these uint32_t so
    // LovyanGFX reads them as RGB888 — truncating to uint16_t is read as RGB565
    // and shows the wrong colour (green -> purple, cyan -> orange).
    uint32_t on  = lcd.color888(0x3F, 0xB9, 0x50), dim = lcd.color888(0x8A, 0x97, 0xA2);
    uint32_t amb = lcd.color888(0xF7, 0xC9, 0x48), red = lcd.color888(0xE5, 0x48, 0x4D);
    lcd.fillRect(0, y - 4, lcd.width(), 28, 0x000000u);
    lcd.drawFastHLine(0, y - 4, lcd.width(), lcd.color888(0x30, 0x36, 0x3d));
    lcd.setTextSize(UI_BAR_TS); lcd.setCursor(UI_PAD, y);
    lcd.setTextColor(pc    ? on : dim, 0x000000u); lcd.print("PC");
    lcd.setTextColor(dim, 0x000000u);              lcd.print(" ");
    lcd.setTextColor(phone ? on : dim, 0x000000u); lcd.print("PH");
    lcd.setTextColor(dim, 0x000000u);              lcd.print(" ");
    lcd.setTextColor(usb   ? on : dim, 0x000000u); lcd.print("USB");
    (void)total;                                                                          // connection count no longer shown
    lcd.setTextColor(autorun ? amb : dim, 0x000000u); lcd.print(autorun ? " A" : " M");   // Auto / Manual
    lcd.setTextColor(armboot ? amb : dim, 0x000000u); lcd.print(" AB");                    // arm-at-boot on(amber)/off(dim)
    if (armboot) {   // only meaningful when arm-at-boot is on -> show its target OS
        const char* tl = targetOs == 1 ? "L" : targetOs == 2 ? "W" : targetOs == 3 ? "M" : "D"; // Linux/Win/Mac/Detect
        lcd.setTextColor(lcd.color888(0x5a, 0xA9, 0xFF), 0x000000u); lcd.print(tl);
    }
    // Battery gauge on the right (hidden when batt<0 — C5, or no fuel gauge).
    if (batt >= 0) {
        uint32_t col = batt > 50 ? on : (batt > 20 ? amb : red);
        int bw = 26, bh = 13, bx = lcd.width() - bw - 8, by = y + 1;
        lcd.drawRect(bx, by, bw, bh, dim);
        lcd.fillRect(bx + bw, by + 3, 2, bh - 6, dim);              // + terminal nub
        int fw = (bw - 4) * batt / 100; if (fw < 0) fw = 0;
        lcd.fillRect(bx + 2, by + 2, fw, bh - 4, col);
        char p[6]; int pw = snprintf(p, sizeof(p), "%d%%", batt);
        lcd.setTextSize(1);
        lcd.setTextColor(dim, 0x000000u); lcd.setCursor(bx - pw * 6 - 4, y + 4); lcd.print(p);
    }
}

#endif  // POC_BOARD_HEADLESS
