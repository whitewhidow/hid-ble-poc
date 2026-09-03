#pragma once
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// HID Consumer Control (usage page 0x0C, HUT 1.4) usage for a short name; 0 = unknown.
// Shared by the BLE and USB paths so the phone can send one name to either. Covers
// the practically-usable Consumer page: power/menu/display, media transport, audio,
// channel, the full Application-Launch (AL) range, and Application-Control (AC)
// navigation / editing / formatting / messaging. Anything not named here can still
// be sent by raw code via ccResolve() ("0x1A7" or a decimal).
static inline uint16_t ccUsage(const char* n) {
    struct { const char* n; uint16_t u; } T[] = {
        // ---- system / power ----
        { "power", 0x0030 }, { "reset", 0x0031 }, { "sleep", 0x0032 }, { "sleepafter", 0x0033 },
        { "sleepmode", 0x0034 }, { "illumination", 0x0035 }, { "functionbuttons", 0x0036 },
        // ---- menu navigation ----
        { "menu", 0x0040 }, { "menupick", 0x0041 }, { "menuup", 0x0042 }, { "menudown", 0x0043 },
        { "menuleft", 0x0044 }, { "menuright", 0x0045 }, { "menuescape", 0x0046 },
        { "menuinc", 0x0047 }, { "menudec", 0x0048 },
        // ---- display ----
        { "dataonscreen", 0x0060 }, { "closedcaption", 0x0061 }, { "snapshot", 0x0065 },
        { "still", 0x0066 }, { "brightup", 0x006F }, { "brightdown", 0x0070 },
        // ---- selection ----
        { "assign", 0x0081 }, { "modestep", 0x0082 }, { "recalllast", 0x0083 },
        { "enterchannel", 0x0084 }, { "ordermovie", 0x0085 },
        { "channelup", 0x009C }, { "channeldown", 0x009D },
        // ---- media transport ----
        { "play", 0x00B0 }, { "pause", 0x00B1 }, { "record", 0x00B2 }, { "ff", 0x00B3 },
        { "rewind", 0x00B4 }, { "next", 0x00B5 }, { "prev", 0x00B6 }, { "stop", 0x00B7 },
        { "eject", 0x00B8 }, { "randomplay", 0x00B9 }, { "repeat", 0x00BC }, { "stopeject", 0x00CC },
        { "playpause", 0x00CD },
        // ---- audio ----
        { "balance", 0x00E1 }, { "mute", 0x00E2 }, { "bass", 0x00E3 }, { "treble", 0x00E4 },
        { "bassboost", 0x00E5 }, { "surround", 0x00E6 }, { "loudness", 0x00E7 },
        { "volup", 0x00E9 }, { "voldown", 0x00EA },
        // ---- application launch (AL) ----
        { "launchconfig", 0x0181 }, { "progbuttonconfig", 0x0182 }, { "mediaplayer", 0x0183 },
        { "wordproc", 0x0184 }, { "texteditor", 0x0185 }, { "spreadsheet", 0x0186 },
        { "graphicseditor", 0x0187 }, { "presentation", 0x0188 }, { "database", 0x0189 },
        { "email", 0x018A }, { "newsreader", 0x018B }, { "voicemail", 0x018C }, { "contacts", 0x018D },
        { "calendar", 0x018E }, { "taskmgr", 0x018F }, { "log", 0x0190 }, { "finance", 0x0191 },
        { "calc", 0x0192 }, { "avcapture", 0x0193 }, { "files", 0x0194 }, { "lanbrowser", 0x0195 },
        { "browser", 0x0196 }, { "remotenetworking", 0x0197 }, { "netconference", 0x0198 },
        { "netchat", 0x0199 }, { "telephony", 0x019A }, { "logon", 0x019B }, { "logoff", 0x019C },
        { "logonoff", 0x019D }, { "lock", 0x019E }, { "controlpanel", 0x019F }, { "commandline", 0x01A0 },
        { "processmanager", 0x01A1 }, { "selecttask", 0x01A2 }, { "nexttask", 0x01A3 },
        { "prevtask", 0x01A4 }, { "halttask", 0x01A5 }, { "helpcenter", 0x01A6 }, { "documents", 0x01A7 },
        { "thesaurus", 0x01A8 }, { "dictionary", 0x01A9 }, { "desktop", 0x01AA }, { "spellcheck", 0x01AB },
        { "grammarcheck", 0x01AC }, { "wirelessstatus", 0x01AD }, { "keyboardlayout", 0x01AE },
        { "virusprotection", 0x01AF }, { "encryption", 0x01B0 }, { "screensaver", 0x01B1 },
        { "alarms", 0x01B2 }, { "clock", 0x01B3 }, { "filebrowser", 0x01B4 }, { "powerstatus", 0x01B5 },
        { "imagebrowser", 0x01B6 }, { "audiobrowser", 0x01B7 }, { "moviebrowser", 0x01B8 },
        { "drm", 0x01B9 }, { "wallet", 0x01BA }, { "im", 0x01BC }, { "oemtips", 0x01BD },
        { "oemhelp", 0x01BE }, { "onlinecommunity", 0x01BF }, { "entertainment", 0x01C0 },
        { "shopping", 0x01C1 }, { "smartcard", 0x01C2 }, { "homefinance", 0x01C3 },
        { "businessfinance", 0x01C4 }, { "reference", 0x01C5 }, { "searchbrowser", 0x01C6 },
        { "audioplayer", 0x01C7 },
        // ---- application control (AC): file / window ----
        { "new", 0x0201 }, { "open", 0x0202 }, { "close", 0x0203 }, { "exit", 0x0204 },
        { "maximize", 0x0205 }, { "minimize", 0x0206 }, { "save", 0x0207 }, { "print", 0x0208 },
        { "properties", 0x0209 },
        // ---- application control (AC): editing ----
        { "undo", 0x021A }, { "copy", 0x021B }, { "cut", 0x021C }, { "paste", 0x021D },
        { "selectall", 0x021E }, { "find", 0x021F }, { "findreplace", 0x0220 }, { "search", 0x0221 },
        { "goto", 0x0222 },
        // ---- application control (AC): browser / navigation ----
        { "webhome", 0x0223 }, { "webback", 0x0224 }, { "webfwd", 0x0225 }, { "webstop", 0x0226 },
        { "webrefresh", 0x0227 }, { "prevlink", 0x0228 }, { "nextlink", 0x0229 }, { "bookmarks", 0x022A },
        { "history", 0x022B }, { "subscriptions", 0x022C }, { "zoomin", 0x022D }, { "zoomout", 0x022E },
        { "zoom", 0x022F }, { "fullscreen", 0x0230 }, { "normalview", 0x0231 }, { "viewtoggle", 0x0232 },
        { "scrollup", 0x0233 }, { "scrolldown", 0x0234 }, { "scroll", 0x0235 }, { "panleft", 0x0236 },
        { "panright", 0x0237 }, { "pan", 0x0238 }, { "newwindow", 0x023A }, { "tilehoriz", 0x023B },
        { "tilevert", 0x023C }, { "format", 0x023D }, { "edit", 0x023E },
        // ---- application control (AC): formatting ----
        { "bold", 0x023F }, { "italics", 0x0240 }, { "underline", 0x0241 }, { "strikethrough", 0x0242 },
        { "subscript", 0x0243 }, { "superscript", 0x0244 }, { "allcaps", 0x0245 }, { "rotate", 0x0246 },
        { "resize", 0x0247 }, { "fliphoriz", 0x0248 }, { "flipvert", 0x0249 },
        { "justifyleft", 0x024C }, { "justifycenter", 0x024E }, { "justifyright", 0x024D },
        { "justifyblock", 0x024F }, { "indent", 0x0257 }, { "outdent", 0x0258 },
        // ---- application control (AC): actions / messaging ----
        { "redo", 0x0279 }, { "reply", 0x0289 }, { "replyall", 0x028A }, { "forwardmsg", 0x028B },
        { "send", 0x028C }, { "attachfile", 0x028D }, { "spellcheck2", 0x028F }, { "cancel", 0x0295 },
        { "next2", 0x029D }, { "prev2", 0x029E },
    };
    for (auto& e : T) if (!strcmp(n, e.n)) return e.u;
    return 0;
}

// Resolve a consumer key: a friendly name OR a raw usage code ("0x1A7" hex, or a
// decimal like "417") so ANY HID Consumer-page usage can be sent, not just the
// named subset. 0 = unresolved.
static inline uint16_t ccResolve(const char* n) {
    uint16_t u = ccUsage(n);
    if (u) return u;
    if (n[0] == '0' && (n[1] == 'x' || n[1] == 'X')) return (uint16_t)strtol(n, nullptr, 16);
    if (n[0] >= '1' && n[0] <= '9') return (uint16_t)strtol(n, nullptr, 10);
    return 0;
}
