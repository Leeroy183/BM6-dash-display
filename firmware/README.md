# Standalone BM6 Dash Firmware

This is the self-contained dash display firmware for BM6 battery monitors and
ESP32-S3 touch displays. It does not require ESPHome, Home Assistant, Wi-Fi, or
the BM6 phone app.

## What it does now

- scans for a BM6 over BLE
- connects and requests the live battery packet
- decrypts the BM6 response on the ESP32
- shows voltage, state of charge, temperature, and connection status
- keeps an in-memory rolling voltage history and plots it on the display

The history is RAM-backed for this first standalone version, so it resets when
the dash loses power. Persistent local history is the next logical step once the
board is confirmed with the real BM6.

## Hardware targets

- Current firmware target: Waveshare ESP32-S3-Touch-LCD-4.3B or 4.3B-BOX
- Planned firmware target: JC4827W543 / Guition-style 4.3 inch ESP32-S3 touch
  display
- BM6 BLE battery monitor
- fused automotive accessory/ignition feed through a suitable automotive power
  supply

The Waveshare board is used through Espressif's `ESP32_Display_Panel` board
support package. The board definition enables the 800 x 480 RGB display, GT911
touch controller, CH422G I/O expander, and backlight handling.

The JC4827W543 target will need its own board/display configuration because it
uses a lower 480 x 272 resolution and different power/display wiring.

## Configure

Edit `src/config.h` before flashing:

- `BM6_MAC_ADDRESS`: set this to your BM6 MAC address for faster and more
  reliable pairing. Leave it as `00:00:00:00:00:00` to scan for the name `BM6`.
- `BM6_POLL_INTERVAL_MS`: how often to poll while the dash is powered.
- voltage warning/critical thresholds for the screen colors.

Keep the BM6 phone app closed while testing. The monitor normally expects a
single BLE central connection at a time.

## Build and flash

From this folder:

```powershell
pio run -t upload
pio device monitor
```

If PlatformIO asks for a port, put the Waveshare board into USB flashing mode
and retry.
