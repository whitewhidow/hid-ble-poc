// Firmware version + self-update source. Bump POC_VERSION on each release and tag
// it (git tag vX.Y.Z); CI publishes the matching app .bin to the GitHub release
// that POC_OTA_URL ("latest") points at, and the board pulls it over WiFi.
#pragma once

// A gitignored dev_secrets.h may define DEV_OTHER_FW_URL to point the firmware
// switch at a self-hosted test bin (no CI build / no billing while iterating).
#if defined(__has_include)
#  if __has_include("dev_secrets.h")
#    include "dev_secrets.h"
#  endif
#endif

#define POC_VERSION "1.1.1"

// Companion Web-Bluetooth console (GitHub Pages) — shown on the boot splash so you
// know where to connect from a phone/PC. Same page for every board.
#define POC_PAGE_URL "whitewhidow.github.io/hid-ble-poc/"

// App-only image (firmware.bin) published by CI on a tag. The in-app updater
// downloads this over WiFi and writes it to the spare OTA slot. Each board pulls
// its own asset (different LCD/pins -> different binary).
#if defined(POC_BOARD_TDONGLE)
#define POC_OTA_URL \
    "https://github.com/whitewhidow/hid-ble-poc/releases/latest/download/hid-ble-poc-app-tdongle.bin"
#elif defined(POC_BOARD_HEADLESS)
#define POC_OTA_URL \
    "https://github.com/whitewhidow/hid-ble-poc/releases/latest/download/hid-ble-poc-app-headless.bin"
#elif defined(POC_BOARD_CARDPUTER)
#define POC_OTA_URL \
    "https://github.com/whitewhidow/hid-ble-poc/releases/latest/download/hid-ble-poc-app-cardputer.bin"
#else
#define POC_OTA_URL \
    "https://github.com/whitewhidow/hid-ble-poc/releases/latest/download/hid-ble-poc-app-tembed.bin"
#endif

// "Switch firmware" target — the sibling project's (BBoink) app bin for THIS same
// physical board. Same ota_0/ota_1 slot layout, so it boots from the spare OTA
// slot via the normal updater; whichever you boot becomes the A/B default (so a
// plugged-in board keeps running it across replugs). Both 16MB boards support it.
#define POC_OTHER_FW_NAME "BBoink"
#if defined(DEV_OTHER_FW_URL)                       // gitignored self-hosted test bin
#define POC_OTHER_FW_URL DEV_OTHER_FW_URL
#elif defined(POC_BOARD_TDONGLE)
#define POC_OTHER_FW_URL \
    "https://github.com/whitewhidow/bboink/releases/latest/download/bboink-app-tdongle-s3.bin"
#elif defined(POC_BOARD_HEADLESS)
#define POC_OTHER_FW_URL ""                          // no BBoink build for the headless board — switch fails cleanly
#elif defined(POC_BOARD_CARDPUTER)
#define POC_OTHER_FW_URL \
    "https://github.com/whitewhidow/bboink/releases/latest/download/bboink-app-cardputer-adv.bin"
#else
#define POC_OTHER_FW_URL \
    "https://github.com/whitewhidow/bboink/releases/latest/download/bboink-app-t-embed-cc1101.bin"
#endif
