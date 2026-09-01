#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "display.h"

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
  #define OFFX     35
#elif defined(POC_BOARD_WAVESHARE)
  #define PIN_SCLK 7
  #define PIN_MOSI 6
  #define PIN_MISO -1
  #define PIN_CS   23
  #define PIN_DC   24
  #define PIN_RST  26
  #define PIN_BL   10
  #define PANEL_W  172
  #define OFFX     34
#else
  #error "define POC_BOARD_TEMBED or POC_BOARD_WAVESHARE"
#endif

class LGFX_Poc : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI      _bus;
    lgfx::Light_PWM    _light;
public:
    LGFX_Poc() {
        { auto c = _bus.config();
          c.spi_host = SPI2_HOST; c.spi_mode = 0; c.freq_write = 40000000; c.freq_read = 16000000;
          c.pin_sclk = PIN_SCLK; c.pin_mosi = PIN_MOSI; c.pin_miso = PIN_MISO; c.pin_dc = PIN_DC;
          _bus.config(c); _panel.setBus(&_bus); }
        { auto c = _panel.config();
          c.pin_cs = PIN_CS; c.pin_rst = PIN_RST; c.pin_busy = -1;
          c.panel_width = PANEL_W; c.panel_height = 320; c.offset_x = OFFX; c.offset_y = 0; c.offset_rotation = 0;
          c.readable = false; c.invert = true; c.rgb_order = false; c.bus_shared = true;
          _panel.config(c); }
        { auto c = _light.config(); c.pin_bl = PIN_BL; c.invert = false; c.freq = 44100; c.pwm_channel = 7;
          _light.config(c); _panel.setLight(&_light); }
        setPanel(&_panel);
    }
};

static LGFX_Poc lcd;

void dispBegin() {
    lcd.init();
    lcd.setRotation(1);              // landscape 320 x (170/172)
    lcd.setBrightness(200);
    lcd.fillScreen(0x000000u);
}

void dispShow(const char* header, const char* body, uint32_t color) {
    lcd.fillScreen(0x000000u);
    lcd.setTextWrap(true);
    lcd.setTextColor(lcd.color888((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF), 0x000000u);
    lcd.setTextSize(3); lcd.setCursor(8, 8);  lcd.print(header);
    lcd.setTextColor(lcd.color888(0xC8, 0xD2, 0xDA), 0x000000u);
    lcd.setTextSize(2); lcd.setCursor(8, 48); lcd.print(body);
}

// Long command at small font (wraps to fit), for showing the one-line payload.
void dispCmd(const char* header, const char* cmd) {
    lcd.fillScreen(0x000000u);
    lcd.setTextWrap(true);
    lcd.setTextColor(lcd.color888(0x22, 0xD3, 0xE0), 0x000000u);
    lcd.setTextSize(2); lcd.setCursor(8, 8); lcd.print(header);
    lcd.setTextColor(lcd.color888(0xC8, 0xD2, 0xDA), 0x000000u);
    lcd.setTextSize(1); lcd.setCursor(8, 34); lcd.print(cmd);
}

// Persistent bottom status bar: PC / PHONE / USB-host as green (up) or red (down),
// plus the total connection count.
void dispBle(bool pc, bool phone, bool usb, int total) {
    int y = lcd.height() - 24;
    // NB: color888() returns 24-bit RGB888 (uint32_t). Keep these uint32_t so
    // LovyanGFX reads them as RGB888 — truncating to uint16_t is read as RGB565
    // and shows the wrong colour (green -> purple, cyan -> orange).
    uint32_t on = lcd.color888(0x3F, 0xB9, 0x50), dim = lcd.color888(0x8A, 0x97, 0xA2);
    lcd.fillRect(0, y - 4, lcd.width(), 28, 0x000000u);
    lcd.drawFastHLine(0, y - 4, lcd.width(), lcd.color888(0x30, 0x36, 0x3d));
    lcd.setTextSize(2); lcd.setCursor(8, y);
    lcd.setTextColor(pc    ? on : dim, 0x000000u); lcd.print("PC");
    lcd.setTextColor(dim, 0x000000u);              lcd.print(" ");
    lcd.setTextColor(phone ? on : dim, 0x000000u); lcd.print("PH");
    lcd.setTextColor(dim, 0x000000u);              lcd.print(" ");
    lcd.setTextColor(usb   ? on : dim, 0x000000u); lcd.print("USB");
    char t[10]; snprintf(t, sizeof(t), " [%d]", total);
    lcd.setTextColor(dim, 0x000000u);              lcd.print(t);
}
