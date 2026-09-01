# hid-ble-poc

USB-HID → BLE-HID **pairing-bootstrap** proof-of-concept, for informational /
educational security research on your own devices.

The idea: a board acts as a **USB HID keyboard** just long enough to script the
host into pairing the board's **own BLE HID keyboard** (Just Works), then it
persists as a **wireless** keyboard over Bluetooth. USB is only used to bootstrap
the pairing — after that everything is BLE.

## One codebase, two boards (build flags)

| env | board | role |
|-----|-------|------|
| `tembed` | LilyGo T-Embed CC1101 (ESP32-S3) | USB HID + BLE HID (full flow) |
| `waveshare` | Waveshare ESP32-C5-LCD-1.47 | BLE HID only (C5 has no USB-OTG; USB stubbed) |

- `POC_HAS_USB_HID` gates the USB side; `POC_BOARD_*` picks display pins + button.
- Shared `ble_hid.cpp` (NimBLE 2.x HID keyboard **+** a custom control service).
- On the S3, the USB side does **LED-fingerprint OS detection** then types the
  pairing stager; the payload lives in `data/<os>.txt` (our own tiny
  `GUI/STRING/ENTER/DELAY` format, on LittleFS).

## Pieces
- `src/` — firmware.
- `data/` — per-OS payload files (LittleFS, tembed env).
- `web/control.html` — phone-side control page (Web Bluetooth): type text into the
  PC over BLE, and manage the board's paired devices. Hosted separately at
  `whitewhidow/hid-ble-poc-web` (also serves the `pair.sh` helper).

## Build / flash
```
pio run -e waveshare -t upload     # C5, auto-reset, no battery
pio run -e tembed   -t upload      # S3 (ARDUINO_USB_MODE=0 for USB HID)
```

Educational PoC — use only on hardware and hosts you own.
