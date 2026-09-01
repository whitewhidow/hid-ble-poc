# hid-ble-poc

USB-HID → BLE-HID **pairing-bootstrap** proof-of-concept, for informational /
educational security research on your own devices.

The idea: a board acts as a **USB HID keyboard** just long enough to script the
host into pairing the board's **own BLE HID keyboard** (Just Works), then it
persists as a **wireless** keyboard over Bluetooth. USB is only used to bootstrap
the pairing — after that everything is BLE.

## Boards

Both targets are ESP32-S3 (native USB → USB-HID, plus BLE-HID), selected by build
flags and sharing one firmware:

| env | board | LCD | battery | notes |
|-----|-------|-----|---------|-------|
| `tembed`  | LilyGo T-Embed CC1101 | ST7789 320×170 | yes (BQ27220) | encoder button |
| `tdongle` | LilyGo T-Dongle S3    | ST7735S 80×160 | no | plugs straight into USB-A · **pins UNVERIFIED until first boot** |

- `POC_BOARD_*` picks display pins/panel + button; `POC_HAS_USB_HID` gates the USB side.
- `display.cpp` parameterises panel type/size/offsets/frequency per board.
- Shared `ble_hid.cpp` (NimBLE 2.x HID keyboard **+** a custom control service).

## What it does

- **Start menu** (single button — click = next, hold = select): **Autodetect**
  (LED-fingerprint the host OS over USB), **Linux / Windows / macOS** (force one),
  and **Sleep** (deep sleep; a button tap wakes it and boots fresh).
- **Payloads** live on LittleFS as `data/<os>.txt` (tiny `GUI/STRING/ENTER/DELAY/
  CTRLALT` format) and are typed over USB when you fire an OS.
- **Boot splash** shows the firmware version + board; the **status bar** shows the
  PC/PH/USB links, the connection count, and a **battery gauge** (T-Embed only).
- **AUTORUN** (arm on select, then auto-fire the payload the moment it's plugged
  into a host) is a **runtime setting** stored in NVS — toggle it from the phone,
  no reflash.

## Phone control page — `docs/index.html`

A Web Bluetooth page (Chrome/Edge, https/localhost), served from GitHub Pages.
Connect to `PoC-KBD`, then use the tabs:

- **Type** — send text to the paired PC over BLE-HID. Each line can be a command
  in the **Evil Crow Cable "Wind"** syntax (`Print`/`PrintLine`/`Press`/`Delay`/
  `Gui*`/`RunWin`/`RunNix`/…); plain lines are typed literally. Full list in the
  **Commands** tab. (Network `Shell*`/`ServerConnect` and `DetectOS` are recognised
  but skipped — no TCP/LED path over BLE.)
- **Payloads** — load/edit the per-OS payload files and save them straight to the
  board's filesystem (no reflash).
- **Devices** — list / forget the board's BLE bonds; drop all links.
- **Update** — give the board WiFi and let it self-update over the air (see below);
  also the AUTORUN toggle.
- **Commands** — the full keystroke-command reference.

## Build / flash

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
  power-on with GPIO0 low → download mode), then flash. *(UNVERIFIED.)*

> Do **not** trigger download mode from firmware (`FORCE_DOWNLOAD_BOOT`): the ROM
> doesn't self-clear that flag and the T-Embed can't be cleanly power-cycled
> without RST, so it can strand you in download mode needing a battery pull. The
> BOOT+RST combo is the safe, deterministic way in.

## Over-the-air self-update

The firmware can update itself over WiFi (A/B OTA partitions):

1. In the phone's **Update** tab, save your **WiFi** SSID + password (stored in
   NVS; the board reconnects on boot).
2. Tap **Update firmware** — the board downloads the latest release, writes the
   spare OTA slot, and reboots into it (progress on phone + LCD).

Releases are cut by tagging: bump `POC_VERSION` in `src/version.h`, then
`git tag vX.Y.Z && git push --tags`. CI (`release.yml`) builds each board and
publishes `hid-ble-poc-app-<board>.bin` to the GitHub release; the board pulls
`releases/latest/download/hid-ble-poc-app-<board>.bin` (see `src/version.h`).

## CI

`.github/workflows/build.yml` builds every env on push/PR and uploads, per board,
the firmware `.bin` (+ `bootloader.bin`, `partitions.bin`, `.elf`) and the
**LittleFS image** as artifacts — a prebuilt image without a local toolchain.

---

Educational PoC — use only on hardware and hosts you own.
