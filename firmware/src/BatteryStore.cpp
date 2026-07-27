#include "BatteryStore.h"

#include <algorithm>
#include <cmath>

void BatteryStore::add(const BatteryReading &reading)
{
    samples_[head_] = reading;
    head_ = (head_ + 1) % HISTORY_CAPACITY;
    if (count_ < HISTORY_CAPACITY) {
        ++count_;
    }
}

bool BatteryStore::latest(BatteryReading &reading) const
{
    if (count_ == 0) {
        return false;
    }

    const size_t latestIndex = (head_ + HISTORY_CAPACITY - 1) % HISTORY_CAPACITY;
    reading = samples_[latestIndex];
    return true;
}

size_t BatteryStore::size() const
{
    return count_;
}

float BatteryStore::minVoltage() const
{
    if (count_ == 0) {
        return 0.0f;
    }

    float minimum = samples_[physicalIndexFromOldest(0)].voltage;
    for (size_t i = 1; i < count_; ++i) {
        minimum = std::min(minimum, samples_[physicalIndexFromOldest(i)].voltage);
    }
    return minimum;
}

float BatteryStore::maxVoltage() const
{
    if (count_ == 0) {
        return 0.0f;
    }

    float maximum = samples_[physicalIndexFromOldest(0)].voltage;
    for (size_t i = 1; i < count_; ++i) {
        maximum = std::max(maximum, samples_[physicalIndexFromOldest(i)].voltage);
    }
    return maximum;
}

int BatteryStore::voltageHundredthsForChart(uint16_t pointIndex, uint16_t pointCount) const
{
    if (count_ == 0 || pointCount == 0) {
        return BATTERY_STORE_NO_CHART_SAMPLE;
    }

    size_t logicalIndex = 0;
    if (count_ <= pointCount) {
        const uint16_t emptyPrefix = pointCount - count_;
        if (pointIndex < emptyPrefix) {
            return BATTERY_STORE_NO_CHART_SAMPLE;
        }
        logicalIndex = pointIndex - emptyPrefix;
    } else if (pointCount == 1) {
        logicalIndex = count_ - 1;
    } else {
        logicalIndex = static_cast<size_t>(
            std::lround((static_cast<double>(pointIndex) * (count_ - 1)) / (pointCount - 1))
        );
    }

    const BatteryReading &sample = samples_[physicalIndexFromOldest(logicalIndex)];
    return static_cast<int>(std::lround(sample.voltage * 100.0f));
}

size_t BatteryStore::physicalIndexFromOldest(size_t logicalIndex) const
{
    const size_t oldest = (count_ == HISTORY_CAPACITY) ? head_ : 0;
    return (oldest + logicalIndex) % HISTORY_CAPACITY;
}
