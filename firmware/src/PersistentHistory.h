#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "BatteryTypes.h"
#include "config.h"

class PersistentHistory {
  public:
    void begin(uint8_t deviceSlot = 0);
    bool addIfDue(const BatteryReading &reading);
    bool latest(BatteryReading &reading) const;
    size_t size() const;
    float minVoltage(size_t recentSamples = HISTORY_CAPACITY) const;
    float maxVoltage(size_t recentSamples = HISTORY_CAPACITY) const;
    int voltageHundredthsForChart(uint16_t pointIndex, uint16_t pointCount,
                                  size_t recentSamples = HISTORY_CAPACITY) const;

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

    struct PersistedMetadata {
        uint32_t magic;
        uint16_t version;
        uint16_t reserved;
        uint32_t next;
        uint32_t count;
    };

    static constexpr uint32_t MAGIC = 0x42364d36;
    static constexpr uint16_t VERSION = 2;
    static constexpr size_t SAMPLES_PER_CHUNK = 16;
    static constexpr size_t CHUNK_COUNT =
        (HISTORY_CAPACITY + SAMPLES_PER_CHUNK - 1) / SAMPLES_PER_CHUNK;

    Preferences preferences_;
    PersistedState state_ = {};
    uint32_t lastStoredAtMs_ = 0;
    bool ready_ = false;
    uint8_t deviceSlot_ = 0;

    void reset();
    bool load();
    bool saveMetadata();
    bool saveChunk(size_t sampleIndex);
    size_t samplesInChunk(size_t chunkIndex) const;
    size_t recentSampleCount(size_t requested) const;
    size_t physicalIndexFromOldest(size_t logicalIndex) const;
    BatteryReading toReading(const HistorySample &sample) const;
};
