# Standalone BM6 Dash Firmware

This is the self-contained dash display firmware for BM6 battery monitors and
ESP32-S3 touch displays. It does not require ESPHome, Home Assistant, Wi-Fi, or
the BM6 phone app.

## What it does now

- scans for a BM6 over BLE
- connects and requests the live battery packet
- decrypts the BM6 response on the ESP32
- shows voltage, state of charge, temperature, and connection status
- keeps a persistent 3-day rolling history and plots it on the display

History is stored in ESP32 NVS as 5-minute samples. The current single-battery
configuration stores 864 samples, covering 3 days of voltage, state of charge,
and temperature while the dash firmware is polling the BM6.

## Hardware targets

- Current firmware target: Waveshare ESP32-S3-Touch-LCD-4.3B or 4.3B-BOX
- Working smoke-test target: JC4827A043_QSPI / JC4827W543-style 4.3 inch
  ESP32-S3 touch display with NV3041A QSPI LCD
- Alternate test target: JC4827W543 / ESP32_4827S043 RGB LCD profile
- BM6 BLE battery monitor
- fused automotive accessory/ignition feed through a suitable automotive power
  supply

The Waveshare board is used through Espressif's `ESP32_Display_Panel` board
support package. The board definition enables the 800 x 480 RGB display, GT911
touch controller, CH422G I/O expander, and backlight handling.

The connected test screen was confirmed as the QSPI/NV3041A variant. It uses a
480 x 272 display, 4 MB flash, 8 MB PSRAM, QSPI LCD pins, and backlight on
GPIO1. Full LVGL dash UI support is the next step.

## Configure

Edit `src/config.h` before flashing:

- `BM6_MAC_ADDRESS`: set this to your BM6 MAC address for faster and more
  reliable pairing. Leave it as `00:00:00:00:00:00` to scan for the name `BM6`.
- `BM6_POLL_INTERVAL_MS`: how often to poll while the dash is powered.
- voltage warning/critical thresholds for the screen colors.

Keep the BM6 phone app closed while testing. The monitor normally expects a
single BLE central connection at a time.

## Bench test a spare BM6

Do not connect a BM6 directly across a 24 V supply. Published BM6 specifications
list a 6-20 V input range for 12 V batteries, so a 24 V supply can damage it.

For a bench test, set a current-limited supply to a normal 12 V battery voltage,
for example 12.6 V. Connect the BM6 red lead to supply positive and black lead
to supply negative. A low current limit such as 50-100 mA is enough for initial
testing because the BM6 normally draws only a few milliamps.

## Build and flash

For the connected QSPI/NV3041A display, from this folder:

```powershell
pio run -e jc4827a043_qspi_dash
pio run -e jc4827a043_qspi_dash -t upload
pio device monitor -e jc4827a043_qspi_dash
```

If PlatformIO asks for a port, select the display USB serial port. The current
test unit appears as `COM5` on this PC.

## JC4827W543 smoke test

For the confirmed AliExpress/Guition-style 4.3 inch ESP32-S3 screen, start with
the QSPI color-bar smoke test:

```powershell
pio run -e jc4827a043_qspi_smoke
pio run -e jc4827a043_qspi_smoke -t upload
pio device monitor -e jc4827a043_qspi_smoke
```

The screen should show color bars with a black status panel. Serial output
should print `JC4827A043 QSPI smoke test starting`, `QSPI display smoke screen
drawn`, and a heartbeat once per second.

## BLE scan diagnostic

If the dash says `BM6 not found`, close the BM6 phone app and run the BLE scan
diagnostic:

```powershell
pio run -e ble_scan_debug -t upload
pio device monitor -e ble_scan_debug
```

Use the BM6 MAC address from the scan output or the phone app in
`src/config.h`. The BM6 usually accepts only one active BLE central, so the app
can prevent the dash from connecting.

The QSPI dash firmware also has an on-screen settings scan. Tap the cog in the
top-right corner to scan nearby BLE devices. Tap `SCAN` to rescan or the
top-left back button to return to the dash. If touch calibration needs work, use
serial commands while monitoring at 115200 baud:

- `s` opens settings and scans
- `r` rescans while on the settings screen
- `d` returns to the dash
