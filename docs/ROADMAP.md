# Roadmap

## First hardware test

- compile the Waveshare firmware target
- confirm BLE scan and connect to a real BM6
- verify decrypted voltage, state of charge, and temperature values
- check screen readability in daylight

## JC4827W543 target

- add a PlatformIO environment for the integrated AliExpress ESP32-S3 display
- confirm LCD controller, touch controller, backlight, flash, and PSRAM config
- adapt the LVGL display layer for 480 x 272

## Multi-battery support

- replace the single BM6 address with a list of named batteries
- poll each BM6 sequentially
- store latest state and history per battery
- add overview, detail, and alert touch screens

## Housing

- decide landscape or portrait orientation
- model the display cradle
- add cable strain relief and mount points
- export printable STL files
