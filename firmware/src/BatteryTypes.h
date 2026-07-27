#pragma once

#include <Arduino.h>

struct BatteryReading {
    float voltage = 0.0f;
    int temperatureC = 0;
    uint8_t socPercent = 0;
    int rssi = 0;
    uint32_t sampledAtMs = 0;
};
