# Roadmap

## First hardware test

- [x] compile the Waveshare firmware target
- [x] identify and flash the connected JC4827A043_QSPI display
- [x] confirm stable display drawing on the QSPI/NV3041A panel
- [ ] confirm BLE scan and connect to a real BM6
- [ ] verify decrypted voltage, state of charge, and temperature values
- [ ] check screen readability in daylight

## JC4827W543 target

- [x] add PlatformIO environments for the integrated AliExpress ESP32-S3 display
- [x] confirm LCD controller, backlight, flash, and PSRAM config
- [x] add standalone QSPI dash screen for 480 x 272
- [x] confirm GT911 touch controller on I2C address 0x5d
- [ ] decide whether to keep the lightweight Arduino_GFX UI or move this target
      to LVGL for richer touch navigation

## Settings

- [x] add a settings cog target on the QSPI dash
- [x] scan and list nearby BLE devices on the display
- [x] persist a selected BM6 address from the settings screen
- [ ] add touch calibration if the tap coordinates are rotated or offset

## Multi-battery support

- replace the single BM6 address with a list of named batteries
- poll each BM6 sequentially
- store latest state and history per battery
- add overview, detail, and alert touch screens

## History

- [x] persist a 3-day single-battery history ring buffer on the ESP32
- [x] graph voltage history on the dash display
- [ ] add per-battery history when multi-battery support lands
- [ ] add real timestamps using RTC or network time if always-accurate clock
      labels are required

## Housing

- decide landscape or portrait orientation
- model the display cradle
- add cable strain relief and mount points
- export printable STL files
