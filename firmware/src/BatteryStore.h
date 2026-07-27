#pragma once

#include <Arduino.h>
#include <limits>
#include "BatteryTypes.h"
#include "config.h"

static constexpr int BATTERY_STORE_NO_CHART_SAMPLE = std::numeric_limits<int>::min();

class BatteryStore {
  public:
    void add(const BatteryReading &reading);
    bool latest(BatteryReading &reading) const;
    size_t size() const;
    float minVoltage() const;
    float maxVoltage() const;
    int voltageHundredthsForChart(uint16_t pointIndex, uint16_t pointCount) const;

  private:
    BatteryReading samples_[HISTORY_CAPACITY];
    size_t head_ = 0;
    size_t count_ = 0;

    size_t physicalIndexFromOldest(size_t logicalIndex) const;
};
