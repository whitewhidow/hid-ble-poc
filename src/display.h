#pragma once
#include <stdint.h>
void dispBegin();
void dispShow(const char* header, const char* body, uint32_t color);  // color = 24-bit RGB
void dispCenter(const char* header, const char* body, uint32_t color); // centered header + body
void dispBle(bool pc, bool phone, bool usb, bool autorun, int batt, int total);  // status bar; batt<0 hides gauge
void dispOff();                                                       // backlight off + panel sleep (before deep sleep)
void dispOn();                                                        // wake panel + backlight on
void dispMenu(const char* title, const char* const* items, int count, int sel);  // selectable list
void dispCmd(const char* header, const char* cmd);                    // long command at small font
