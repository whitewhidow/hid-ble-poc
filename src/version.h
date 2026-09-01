// Firmware version + self-update source. Bump POC_VERSION on each release and tag
// it (git tag vX.Y.Z); CI publishes the matching app .bin to the GitHub release
// that POC_OTA_URL ("latest") points at, and the board pulls it over WiFi.
#pragma once

#define POC_VERSION "0.1.10"

// App-only image (firmware.bin) published by CI on a tag. The in-app updater
// downloads this over WiFi and writes it to the spare OTA slot. Each board pulls
// its own asset (different LCD/pins -> different binary).
#if defined(POC_BOARD_TDONGLE)
#define POC_OTA_URL \
    "https://github.com/whitewhidow/hid-ble-poc/releases/latest/download/hid-ble-poc-app-tdongle.bin"
#else
#define POC_OTA_URL \
    "https://github.com/whitewhidow/hid-ble-poc/releases/latest/download/hid-ble-poc-app-tembed.bin"
#endif
