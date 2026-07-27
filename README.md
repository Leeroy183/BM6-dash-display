# BM6 Dash Display

Open-source dash display firmware and housing files for monitoring one or more
BM6 Bluetooth car battery monitors from an ESP32-S3 touch display.

## Project goals

- run standalone in the vehicle without Home Assistant, ESPHome, or Wi-Fi
- support multiple BM6 battery monitors by polling them sequentially over BLE
- show live voltage, state of charge, temperature, status, and warning states
- keep local history per battery for trend and fault review
- provide printable enclosure STL files and editable CAD source files

## Current hardware targets

The first firmware target is a 4.3 inch ESP32-S3 touch display.

- Primary target under evaluation: JC4827W543 / Guition-style 4.3 inch
  ESP32-S3 capacitive touch display
- Reference target: Waveshare ESP32-S3-Touch-LCD-4.3B or 4.3B-BOX

The firmware currently includes the Waveshare board target because it has strong
library support and public board definitions. A JC4827W543 target will be added
next so the lower-cost integrated ESP32-S3 display can become the main dash
build.

## Repository layout

- `firmware/` - PlatformIO/Arduino standalone firmware
- `housing/` - enclosure notes, CAD source placeholder, and STL placeholder
- `docs/` - wiring, protocol, hardware, and attribution notes

## Firmware status

The first standalone firmware can:

- scan for a BM6 by MAC address or advertised name
- connect over BLE
- subscribe to the BM6 notification characteristic
- send the BM6 read command
- decrypt and parse voltage, temperature, and state of charge
- draw a local LVGL dash screen with rolling voltage history

This is ready for first hardware compilation and BM6 testing. Persistent
history, multi-battery screens, touch navigation, and the JC4827W543 board
target are planned next.

## Build

Install PlatformIO, then from `firmware/`:

```powershell
pio run
pio run -t upload
pio device monitor
```

Before flashing, edit `firmware/src/config.h` and set your BM6 MAC address.

## License

This project is released under the MIT License. See `LICENSE`.

Third-party source files keep their own SPDX headers. Protocol research and
credits are tracked in `docs/ATTRIBUTION.md`.
