#pragma once
#include <stdint.h>
#include <string.h>

// HID Consumer Control (usage page 0x0C) usage for a short name; 0 = unknown.
// Shared by the BLE and USB paths so the phone can send one name to either.
// Covers media transport, volume, brightness, and the AL/AC application keys
// (calculator, email, browser nav, etc.) — all within the 0x000-0x3FF window the
// report descriptors declare.
static inline uint16_t ccUsage(const char* n) {
    struct { const char* n; uint16_t u; } T[] = {
        // media transport
        { "play", 0x00B0 }, { "pause", 0x00B1 }, { "playpause", 0x00CD }, { "stop", 0x00B7 },
        { "next", 0x00B5 }, { "prev", 0x00B6 }, { "ff", 0x00B3 }, { "rewind", 0x00B4 },
        { "record", 0x00B2 }, { "eject", 0x00B8 },
        // audio
        { "mute", 0x00E2 }, { "volup", 0x00E9 }, { "voldown", 0x00EA },
        // display
        { "brightup", 0x006F }, { "brightdown", 0x0070 },
        // application launch (AL)
        { "calc", 0x0192 }, { "email", 0x018A }, { "files", 0x0194 }, { "browser", 0x0196 },
        { "mediaplayer", 0x0183 }, { "wordproc", 0x0184 }, { "spreadsheet", 0x0186 },
        { "calendar", 0x018E }, { "contacts", 0x018D }, { "controlpanel", 0x019F },
        { "taskmgr", 0x01A2 }, { "lock", 0x019E }, { "logoff", 0x019C },
        // application control (AC) — browser nav
        { "webhome", 0x0223 }, { "webback", 0x0224 }, { "webfwd", 0x0225 }, { "webstop", 0x0226 },
        { "webrefresh", 0x0227 }, { "bookmarks", 0x022A }, { "search", 0x0221 },
    };
    for (auto& e : T) if (!strcmp(n, e.n)) return e.u;
    return 0;
}
