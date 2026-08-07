# BM6 Dash Display

Open-source dash display firmware and housing files for monitoring one or more
BM6 Bluetooth car battery monitors from an ESP32-S3 touch display.

## Project goals

- run standalone in the vehicle without Home Assistant, ESPHome, or Wi-Fi
- save multiple BM6 monitors and keep a persistent BLE stream to the selected battery
- show live voltage, state of charge, temperature, status, and warning states
- keep local history per battery for trend and fault review
- provide printable enclosure STL files and editable CAD source files

## Current hardware targets

The first firmware target is a 4.3 inch ESP32-S3 touch display.

- Primary target: JC4827A043_QSPI / Guition-style 4.3 inch ESP32-S3
  capacitive touch display with NV3041A QSPI LCD
- Reference target: Waveshare ESP32-S3-Touch-LCD-4.3B or 4.3B-BOX

The connected AliExpress display has been verified as the QSPI/NV3041A variant.
It is the main dash build target for the first in-car prototype.

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
- draw a local dash screen on the QSPI display
- keep a persistent 3-day ring buffer of voltage, state of charge, and
  temperature samples

Multi-battery screens, touch navigation, and STL housing files are planned next.

## Build

Install PlatformIO, then from `firmware/`:

```powershell
pio run -e jc4827a043_qspi_dash
pio run -e jc4827a043_qspi_dash -t upload
pio device monitor -e jc4827a043_qspi_dash
```

Before flashing, edit `firmware/src/config.h` and set your BM6 MAC address.
Leave it as `00:00:00:00:00:00` only when scanning by the advertised name
`BM6`.

## License

This project is released under the MIT License. See `LICENSE`.

Third-party source files keep their own SPDX headers. Protocol research and
credits are tracked in `docs/ATTRIBUTION.md`.
