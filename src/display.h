#pragma once
#include <stdint.h>
void dispBegin();
void dispShow(const char* header, const char* body, uint32_t color);  // color = 24-bit RGB
void dispBle(bool pc, bool phone, bool usb, int batt, int total);     // status bar; batt<0 hides the gauge
void dispOff();                                                       // backlight off + panel sleep (before deep sleep)
void dispCmd(const char* header, const char* cmd);                    // long command at small font
