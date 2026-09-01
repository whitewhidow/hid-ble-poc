#pragma once
#include <stdint.h>
void dispBegin();
void dispShow(const char* header, const char* body, uint32_t color);  // color = 24-bit RGB
void dispBle(bool pc, bool phone, bool usb, int total);               // persistent bottom status bar
void dispCmd(const char* header, const char* cmd);                    // long command at small font
