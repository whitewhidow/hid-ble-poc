# hid-ble-poc

**Turn an ESP32-S3 into a wireless keyboard that can type scripted keystrokes into a
nearby computer — first over USB, then over Bluetooth once it has paired itself.** A
proof-of-concept for informational / educational security research on hardware and
hosts **you own**.

USB-HID → BLE-HID **pairing-bootstrap** proof-of-concept. ("HID" = Human Interface
Device — the keyboard/mouse class.)

The idea: a board acts as a **USB HID keyboard** just long enough to script the host
into pairing the board's **own BLE HID keyboard** (using BLE's no-PIN *"Just Works"*
pairing mode), then it persists as a **wireless** keyboard over Bluetooth. USB is only
used to bootstrap the pairing — after that everything is BLE.

## Boards

All targets are ESP32-S3 (native USB → USB-HID, plus BLE-HID), selected by build
flags and sharing one firmware:

| env | board | LCD | battery | notes |
|-----|-------|-----|---------|-------|
| `tembed`   | LilyGo T-Embed CC1101 | ST7789 320×170 | yes (BQ27220) | encoder button |
| `tdongle`  | LilyGo T-Dongle S3    | ST7735S 80×160 | no | plugs straight into USB-A · verified on hardware (smaller per-board UI metrics; menu drops **Sleep** since it's USB-powered) |
| `cardputer` | M5Cardputer ADV (StampS3, 8 MB) | ST7789 240×135 | yes (GPIO10 ×2) | verified on hardware · in the BBoink/BBportal switch mesh · USB flash needs manual download mode (hold **G0**, tap **RESET**) as it runs native USB-HID |
| `headless` | Generic ESP32-S3 module (8 MB, no PSRAM) | none | no | **no display, no button** — the phone portal is the entire UI. USB-HID + BLE-HID + BLE control all work; 8 MB A/B partition so WiFi self-update works. Verified on hardware. Reflash needs manual download mode (GPIO0→GND while plugging, no BOOT button). |

- `POC_BOARD_*` picks display pins/panel + button; `POC_HAS_USB_HID` gates the USB side.
- `display.cpp` parameterises panel type/size/offsets/frequency per board — or **no-ops entirely** on `headless` (null display; the BLE portal is the UI).
- Shared `ble_hid.cpp` (NimBLE 2.x HID keyboard **+** a custom control service).
- The **firmware switch** to [BBoink](https://github.com/whitewhidow/bboink) / [BBportal](https://github.com/whitewhidow/bb-portal) is on the S3 mesh boards `tembed`/`tdongle`/`cardputer` (each has counterpart builds + a shared A/B partition layout); it no-ops on `headless`.

## What it does

- **Start menu** (single button — click = next, hold = select): **Autodetect**
  (LED-fingerprint the host OS over USB), **Linux / Windows / macOS**, and a
  free-form **Custom** slot — plus **Sleep** on the T-Embed (deep sleep; a button
  tap wakes it and boots fresh; the USB-powered T-Dongle omits Sleep). Firing types
  that slot's payload over USB.
- **Payload library** (managed from the phone): keep any number of **named**
  payloads on LittleFS and **load** one into each OS **slot** (linux/windows/macos/
  custom) — the slot is what the board fires. Payload syntax is the Evil Crow "Wind"
  set — a DuckyScript-like keystroke language (`Print`/`STRING`/`ENTER`/`Delay`/`Gui*`/
  `RunWin`/…; `#`/`REM` comments); see the **Commands** tab in the phone page for the full list.
- **On-screen keyboard + live typing** from the phone, over **BLE-HID or USB**
  (see the control page below).
- **Graphical boot splash** (version + board; skippable in Options, and auto-skipped
  when arming at boot). The **status bar** shows PC/PH/USB links, AUTORUN (`A`/`M`),
  arm-at-boot (`AB` + the target-OS letter when on), and a **battery gauge** (T-Embed + Cardputer).
- **AUTORUN** (arm on select → auto-fire on plug) and **arm-at-boot** (headless) are
  runtime NVS settings, set from the phone — no reflash.
- **Remote control over WiFi** (see [`relay/`](relay/)) — drive the board from beyond BLE
  range via a tiny relay you host: it joins WiFi and long-polls the relay for the *same*
  control commands, so the phone page runs it over the internet (TLS keep-alive + batching
  keep typing responsive). The S3 keeps BLE-HID live alongside; set the relay URL in
  **Options → Go remote**, or **Find open AP + go remote** to scan for an open Wi-Fi that can
  reach the relay (it verifies `/health` so captive portals are rejected, and falls back to
  your saved creds). The mailbox id is stable across boots (derived from the factory MAC; a
  custom id is settable), and payload load/save + settings run over the relay with per-chunk
  retry so a dropped reply just re-requests. The STA marker on the display shows the link
  state (orange connecting, blinking blue scanning, solid blue on an open AP, green on saved
  creds), and the **Board feedback** panel echoes every action and reply. Handy for a headless
  dongle deployed somewhere and driven remotely.

## Phone control page — `docs/index.html`

A Web Bluetooth page (Chrome/Edge, https/localhost), served from GitHub Pages.
Connect to `PoC-KBD`; a unified header shows the connection, firmware version, and
**BLE-HID / USB** status. Tabs:

- **Keyboard** — an on-screen **QWERTY** (Shift latches; Ctrl/Alt/⊞ chord the next
  letter; Backspace/Enter/Tab/Esc/arrows/Ctrl-Alt-Del) plus a **script/text box**
  (Evil Crow "Wind" syntax; plain lines typed literally). A **BLE-HID ⇄ USB**
  dropdown routes every key **and** the script to the BLE-paired PC or the
  USB-plugged PC.
- **Payloads** — the payload **library**: add / edit / delete named payloads, then
  **Load** one into each OS slot. **Fire** any slot (Linux/Windows/macOS/Custom)
  over BLE **or** USB, plus **Autodetect + Run** (USB-only fingerprint → run).
- **Devices** — list / forget the board's BLE bonds; drop all links.
- **WiFi / Update** — provision WiFi and self-update over the air (see below).
- **Options** — AUTORUN (Run/Arm), arm-at-boot (Manual arm + target OS), fire /
  type delays, the boot-splash toggle, and the **relay** settings (URL/token, connect-on-boot,
  find-open-AP, force-keep-BLE, custom mailbox id) with **Go remote** / **Find open AP + go remote**.
- **Board feedback** panel — every action you take echoes as `→` and the board's replies as `←`
  (background polls and live typing are filtered out), so you always see whether a command took.
- **Commands** — the full keystroke-command reference. (Network `Shell*`/
  `ServerConnect` and `DetectOS` are recognised but skipped — no TCP/LED path over BLE.)

## Build / flash

**No toolchain?** Use the browser **[web flasher](https://whitewhidow.github.io/hid-ble-poc/flasher/)**
(Chrome/Edge desktop, Web Serial) — pick your board + version and Install. It flashes the
merged image (bootloader + partitions + app) over USB. Put the board in download mode first
(T-Embed: hold BOOT + tap RESET; T-Dongle: hold button while plugging; Cardputer: hold G0 + tap RESET; headless: GPIO0→GND).

From source:

```
pio run -e tembed  -t buildfs     # build the LittleFS payload image (data/ -> littlefs.bin)
pio run -e tembed  -t uploadfs    # flash the payloads (only after editing data/*.txt)
pio run -e tembed  -t upload      # flash the firmware
```

(swap `-e tembed` for `-e tdongle` for the dongle.)

### Entering download mode

Neither board's USB auto-reset reliably reaches the boot straps while the app
holds native USB, so enter the bootloader by hand:

- **T-Embed** — hold **BOOT** (encoder push), tap **RST**, release BOOT. It runs
  off its battery, so *replugging USB does nothing* — only **RST** resets the SoC.
  Flash **filesystem first, then app** (both connect while it's in download mode;
  the post-flash reset usually leaves it in the bootloader, so you can flash again
  without repeating this). Then tap **RST** to boot.
- **T-Dongle S3** (no battery) — hold **BOOT** while plugging it into USB (a
  power-on with GPIO0 low → download mode), then flash. The **first** flash from
  factory firmware auto-resets fine, but once the PoC is running it holds native USB
  in **USB-HID (TinyUSB) mode**, so esptool can no longer auto-reset it — every
  reflash after that needs this BOOT-while-plugging step (else esptool reports
  "No serial data received").

> Do **not** trigger download mode from firmware (`FORCE_DOWNLOAD_BOOT`): the ROM
> doesn't self-clear that flag and the T-Embed can't be cleanly power-cycled
> without RST, so it can strand you in download mode needing a battery pull. The
> BOOT+RST combo is the safe, deterministic way in.

## Over-the-air self-update

The firmware can update itself over WiFi (A/B OTA partitions):

1. In the phone's **WiFi** tab, save your **WiFi** SSID + password (stored in NVS).
   WiFi only needs to be *configured* — it's never brought up live over the portal
   (WiFi + BLE at once churns the radio); the board connects it itself at the
   reboot.
2. In the **Update** tab, tap **Update firmware** — the board flags a fetch and
   reboots; at a clean heap it connects WiFi, writes the spare OTA slot, and boots
   the new image (progress on the LCD).

**Switch firmware:** a hidden action (tap the title 3×) lists the sibling apps for
this board — [BBoink](https://github.com/whitewhidow/bboink) (a WiFi handshake hunter)
and [BBportal](https://github.com/whitewhidow/bb-portal) (a T&C captive portal) — and
flashes the chosen one into the spare slot, booting into it (same OTA machinery,
byte-compatible slots). Switch back from that app's own portal. Offered on the three
S3 mesh boards `tembed` / `tdongle` / `cardputer`; `headless` is standalone. Each
firmware advertises a distinct BLE address so the host's GATT cache doesn't collide
across a switch.

Releases are cut by tagging: bump `POC_VERSION` in `src/version.h`, then
`git tag vX.Y.Z && git push --tags`. CI (`release.yml`) builds each board and
publishes `hid-ble-poc-app-<board>.bin` to the GitHub release; the board pulls
`releases/latest/download/hid-ble-poc-app-<board>.bin` (see `src/version.h`).

## CI

`.github/workflows/build.yml` builds every env on push/PR and uploads, per board,
the firmware `.bin` (+ `bootloader.bin`, `partitions.bin`, `.elf`) and the
**LittleFS image** as artifacts — a prebuilt image without a local toolchain.

## Getting help

Questions, bugs, or stuck on setup? **Please open a GitHub issue** — that's the place to
ask for help: <https://github.com/whitewhidow/hid-ble-poc/issues>. Include your board,
firmware version, and what you tried.

---

Educational PoC — use only on hardware and hosts you own.
