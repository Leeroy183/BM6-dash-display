# Roadmap

## First hardware test

- [x] compile the Waveshare firmware target
- [x] identify and flash the connected JC4827A043_QSPI display
- [x] confirm stable display drawing on the QSPI/NV3041A panel
- [x] confirm BLE scan and connect to a real BM6
- [x] verify decrypted voltage, state of charge, and temperature values
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
- [x] align the GT911 touch coordinates with the display orientation

## Multi-battery support

- [x] save and rename up to four batteries
- [x] switch the persistent BLE stream between saved batteries
- [x] store latest state and history per battery
- [ ] add a combined multi-battery overview and alerts

## History

- [x] persist a 3-day single-battery history ring buffer on the ESP32
- [x] graph voltage history on the dash display
- [x] add per-battery ESP32 history
- [ ] reverse engineer BM6 onboard-history synchronization
- [ ] import offline voltage and cranking records from the BM6
- [ ] add real timestamps using RTC or network time if always-accurate clock
      labels are required

## Housing

- decide landscape or portrait orientation
- model the display cradle
- add cable strain relief and mount points
- export printable STL files
