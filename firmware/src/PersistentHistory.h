#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "BatteryTypes.h"
#include "config.h"

class PersistentHistory {
  public:
    void begin();
    bool addIfDue(const BatteryReading &reading);
    bool latest(BatteryReading &reading) const;
    size_t size() const;
    float minVoltage() const;
    float maxVoltage() const;
    int voltageHundredthsForChart(uint16_t pointIndex, uint16_t pointCount) const;

  private:
    struct HistorySample {
        uint32_t minuteMark;
        uint16_t voltageCenti;
        int8_t temperatureC;
        uint8_t socPercent;
    };

    struct PersistedState {
        uint32_t magic;
        uint16_t version;
        uint16_t reserved;
        uint32_t next;
        uint32_t count;
        HistorySample samples[HISTORY_CAPACITY];
    };

    static constexpr uint32_t MAGIC = 0x42364d36;
    static constexpr uint16_t VERSION = 1;

    Preferences preferences_;
    PersistedState state_ = {};
    uint32_t lastStoredAtMs_ = 0;

    void reset();
    void save();
    size_t physicalIndexFromOldest(size_t logicalIndex) const;
    BatteryReading toReading(const HistorySample &sample) const;
};
