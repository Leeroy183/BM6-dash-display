# Attribution

This project builds on public BM6 interoperability research.

## BM6 protocol research

The BLE UUIDs, encrypted read command, AES key, and response packet layout were
first implemented publicly in JeffWDH's BM6 battery monitor project:

- https://github.com/JeffWDH/bm6-battery-monitor
- https://www.tarball.ca/posts/reverse-engineering-the-bm6-ble-battery-monitor/

That upstream repository did not include a license when this project was
created, so this project is intended to use independently written code while
crediting the protocol research.

## Display support

The Waveshare target uses Espressif's Arduino display libraries:

- https://github.com/esp-arduino-libs/ESP32_Display_Panel
- https://github.com/esp-arduino-libs/ESP32_IO_Expander
- https://github.com/esp-arduino-libs/esp-lib-utils

The included LVGL porting files are copied from Espressif examples and retain
their original SPDX notices.

## Bluetooth support

BLE central/client support uses NimBLE-Arduino:

- https://github.com/h2zero/NimBLE-Arduino
