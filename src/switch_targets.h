// Firmware-switch targets: the OTHER apps' latest-release app-bin for THIS board.
// PoC ships for S3 only; the switch mesh is the two 16MB boards (T-Embed, T-Dongle),
// which share an identical A/B partition table with BBoink and Terms Portal.
// A gitignored dev_secrets.h may define DEV_OTHER_FW_URL to override target 0.
#pragma once

struct SwitchTarget { const char* name; const char* url; };

#define GH_ "https://github.com/whitewhidow/"

#if defined(POC_BOARD_TDONGLE)
static const SwitchTarget SWITCH_TARGETS[] = {
#if defined(DEV_OTHER_FW_URL)
  { "BBoink",       DEV_OTHER_FW_URL },
#else
  { "BBoink",       GH_ "bboink/releases/latest/download/bboink-app-tdongle-s3.bin" },
#endif
  { "BBportal"    , GH_ "bb-portal/releases/latest/download/bb-portal-app-tdongle-s3.bin" },
};
static const int SWITCH_TARGET_COUNT = (int)(sizeof(SWITCH_TARGETS) / sizeof(SWITCH_TARGETS[0]));
#elif defined(POC_BOARD_TEMBED)
static const SwitchTarget SWITCH_TARGETS[] = {
#if defined(DEV_OTHER_FW_URL)
  { "BBoink",       DEV_OTHER_FW_URL },
#else
  { "BBoink",       GH_ "bboink/releases/latest/download/bboink-app-t-embed-cc1101.bin" },
#endif
  { "BBportal"    , GH_ "bb-portal/releases/latest/download/bb-portal-app-tembed-cc1101.bin" },
};
static const int SWITCH_TARGET_COUNT = (int)(sizeof(SWITCH_TARGETS) / sizeof(SWITCH_TARGETS[0]));
#else
static const SwitchTarget SWITCH_TARGETS[1] = { { "", "" } };   // headless / not in the mesh
static const int SWITCH_TARGET_COUNT = 0;
#endif

#undef GH_
