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
  `whitewhidow/hid-ble-poc` (also serves the `pair.sh` helper).

## Build / flash

```
pio run -e waveshare -t upload                 # C5: builds + flashes, auto-reset (no battery)

pio run -e tembed   -t buildfs                  # build the LittleFS payload image (data/ -> littlefs.bin)
pio run -e tembed   -t uploadfs                 # flash the payloads   (needed after editing data/*.txt)
pio run -e tembed   -t upload                   # flash the firmware
```

The **C5 (waveshare)** has no battery and auto-resets over USB-Serial-JTAG, so a
plain `-t upload` just works.

### Flashing the T-Embed manually (the battery gotcha)

The **T-Embed CC1101** runs off its battery and its USB auto-reset (RTS/DTR) does
**not** reach the boot straps, so esptool can't put it into download mode on its
own. You do it by hand:

1. **Enter download mode:** hold **BOOT** (the encoder push), tap **RST**, then
   release BOOT. The board is now in the USB download bootloader.
   - Replugging USB does **nothing** here — the battery keeps the chip powered.
     Only the **RST** button actually resets the SoC (it pulls EN/CHIP_PU).
2. **Flash filesystem first, then app** (both connect while it's in download mode;
   on this board the post-flash "hard reset via RTS" usually doesn't fire, so it
   stays in the bootloader and you can flash again without repeating step 1):
   ```
   pio run -e tembed -t uploadfs      # only if data/*.txt changed
   pio run -e tembed -t upload
   ```
3. **Boot the new firmware:** tap **RST** once. (Don't rely on esptool's auto
   hard-reset on this board.)

> Do **not** try to trigger download mode from firmware (`FORCE_DOWNLOAD_BOOT`).
> The ROM doesn't self-clear that flag and this board can't be cleanly
> power-cycled without RST, so it can strand you in download mode needing a
> battery pull. The BOOT+RST button combo is the safe, deterministic way in.

`-t uploadfs` is only needed when the `data/*.txt` payloads change; a firmware-only
edit just needs `-t upload`.

## CI

`.github/workflows/build.yml` builds **both** envs on every push/PR and uploads,
per board, the firmware `.bin` (+ `bootloader.bin`, `partitions.bin`, `.elf`) and
the **LittleFS image** as downloadable artifacts — so you can grab a prebuilt
`tembed-firmware.bin` / `tembed-littlefs.bin` without a local toolchain.

Educational PoC — use only on hardware and hosts you own.
