#include "PersistentHistory.h"

#include <algorithm>
#include <cmath>

void PersistentHistory::begin()
{
    preferences_.begin("bm6hist", false);
    const size_t loaded = preferences_.getBytes("state", &state_, sizeof(state_));
    if (loaded != sizeof(state_) || state_.magic != MAGIC || state_.version != VERSION ||
        state_.next >= HISTORY_CAPACITY || state_.count > HISTORY_CAPACITY) {
        reset();
        save();
    }
}

bool PersistentHistory::addIfDue(const BatteryReading &reading)
{
    if (lastStoredAtMs_ != 0 && millis() - lastStoredAtMs_ < HISTORY_SAMPLE_INTERVAL_MS) {
        return false;
    }

    HistorySample sample = {};
    sample.minuteMark = millis() / 60000UL;
    sample.voltageCenti = static_cast<uint16_t>(std::lround(reading.voltage * 100.0f));
    sample.temperatureC = static_cast<int8_t>(std::max(-128, std::min(127, reading.temperatureC)));
    sample.socPercent = reading.socPercent;

    state_.samples[state_.next] = sample;
    state_.next = (state_.next + 1) % HISTORY_CAPACITY;
    if (state_.count < HISTORY_CAPACITY) {
        ++state_.count;
    }
    lastStoredAtMs_ = millis();
    save();
    return true;
}

bool PersistentHistory::latest(BatteryReading &reading) const
{
    if (state_.count == 0) {
        return false;
    }
    const size_t index = (state_.next + HISTORY_CAPACITY - 1) % HISTORY_CAPACITY;
    reading = toReading(state_.samples[index]);
    return true;
}

size_t PersistentHistory::size() const
{
    return state_.count;
}

float PersistentHistory::minVoltage() const
{
    if (state_.count == 0) {
        return 0.0f;
    }

    uint16_t minimum = state_.samples[physicalIndexFromOldest(0)].voltageCenti;
    for (size_t i = 1; i < state_.count; ++i) {
        minimum = std::min(minimum, state_.samples[physicalIndexFromOldest(i)].voltageCenti);
    }
    return minimum / 100.0f;
}

float PersistentHistory::maxVoltage() const
{
    if (state_.count == 0) {
        return 0.0f;
    }

    uint16_t maximum = state_.samples[physicalIndexFromOldest(0)].voltageCenti;
    for (size_t i = 1; i < state_.count; ++i) {
        maximum = std::max(maximum, state_.samples[physicalIndexFromOldest(i)].voltageCenti);
    }
    return maximum / 100.0f;
}

int PersistentHistory::voltageHundredthsForChart(uint16_t pointIndex, uint16_t pointCount) const
{
    if (state_.count == 0 || pointCount == 0) {
        return -1;
    }

    size_t logicalIndex = 0;
    if (state_.count <= pointCount) {
        const uint16_t emptyPrefix = pointCount - state_.count;
        if (pointIndex < emptyPrefix) {
            return -1;
        }
        logicalIndex = pointIndex - emptyPrefix;
    } else if (pointCount == 1) {
        logicalIndex = state_.count - 1;
    } else {
        logicalIndex = static_cast<size_t>(
            std::lround((static_cast<double>(pointIndex) * (state_.count - 1)) / (pointCount - 1))
        );
    }

    return state_.samples[physicalIndexFromOldest(logicalIndex)].voltageCenti;
}

void PersistentHistory::reset()
{
    state_ = {};
    state_.magic = MAGIC;
    state_.version = VERSION;
}

void PersistentHistory::save()
{
    preferences_.putBytes("state", &state_, sizeof(state_));
}

size_t PersistentHistory::physicalIndexFromOldest(size_t logicalIndex) const
{
    const size_t oldest = (state_.count == HISTORY_CAPACITY) ? state_.next : 0;
    return (oldest + logicalIndex) % HISTORY_CAPACITY;
}

BatteryReading PersistentHistory::toReading(const HistorySample &sample) const
{
    BatteryReading reading;
    reading.voltage = sample.voltageCenti / 100.0f;
    reading.temperatureC = sample.temperatureC;
    reading.socPercent = sample.socPercent;
    reading.sampledAtMs = sample.minuteMark * 60000UL;
    return reading;
}
