#include "PersistentHistory.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <nvs_flash.h>

namespace {
constexpr char HISTORY_PARTITION[] = "history";
constexpr char METADATA_KEY[] = "meta";
}

void PersistentHistory::begin(uint8_t deviceSlot)
{
    if (ready_) {
        preferences_.end();
    }
    ready_ = false;
    lastStoredAtMs_ = 0;
    deviceSlot_ = deviceSlot;
    reset();

    esp_err_t initResult = nvs_flash_init_partition(HISTORY_PARTITION);
    if (initResult == ESP_ERR_NVS_NO_FREE_PAGES || initResult == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase_partition(HISTORY_PARTITION);
        initResult = nvs_flash_init_partition(HISTORY_PARTITION);
    }
    char historyNamespace[16];
    if (deviceSlot_ == 0) {
        std::snprintf(historyNamespace, sizeof(historyNamespace), "bm6hist");
    } else {
        std::snprintf(historyNamespace, sizeof(historyNamespace), "bm6hist%u",
                      static_cast<unsigned>(deviceSlot_));
    }
    if (initResult != ESP_OK || !preferences_.begin(historyNamespace, false, HISTORY_PARTITION)) {
        Serial.printf("BM6 history partition unavailable: %s\n", esp_err_to_name(initResult));
        reset();
        return;
    }

    ready_ = true;
    if (!load()) {
        preferences_.clear();
        reset();
        saveMetadata();
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

    const size_t sampleIndex = state_.next;
    state_.samples[sampleIndex] = sample;
    state_.next = (state_.next + 1) % HISTORY_CAPACITY;
    if (state_.count < HISTORY_CAPACITY) {
        ++state_.count;
    }
    lastStoredAtMs_ = millis();
    if (ready_ && (!saveChunk(sampleIndex) || !saveMetadata())) {
        Serial.println("BM6 history save failed");
    }
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

float PersistentHistory::minVoltage(size_t recentSamples) const
{
    const size_t sampleCount = recentSampleCount(recentSamples);
    if (sampleCount == 0) {
        return 0.0f;
    }

    const size_t first = state_.count - sampleCount;
    uint16_t minimum = state_.samples[physicalIndexFromOldest(first)].voltageCenti;
    for (size_t i = first + 1; i < state_.count; ++i) {
        minimum = std::min(minimum, state_.samples[physicalIndexFromOldest(i)].voltageCenti);
    }
    return minimum / 100.0f;
}

float PersistentHistory::maxVoltage(size_t recentSamples) const
{
    const size_t sampleCount = recentSampleCount(recentSamples);
    if (sampleCount == 0) {
        return 0.0f;
    }

    const size_t first = state_.count - sampleCount;
    uint16_t maximum = state_.samples[physicalIndexFromOldest(first)].voltageCenti;
    for (size_t i = first + 1; i < state_.count; ++i) {
        maximum = std::max(maximum, state_.samples[physicalIndexFromOldest(i)].voltageCenti);
    }
    return maximum / 100.0f;
}

int PersistentHistory::voltageHundredthsForChart(
    uint16_t pointIndex, uint16_t pointCount, size_t recentSamples
) const
{
    const size_t sampleCount = recentSampleCount(recentSamples);
    if (sampleCount == 0 || pointCount == 0 || recentSamples == 0) {
        return -1;
    }

    const size_t rangeSlot = pointCount == 1
        ? recentSamples - 1
        : static_cast<size_t>(std::lround(
            (static_cast<double>(pointIndex) * (recentSamples - 1)) / (pointCount - 1)
        ));
    const size_t emptySlots = recentSamples - sampleCount;
    if (rangeSlot < emptySlots) {
        return -1;
    }

    const size_t sampleOffset = std::min(sampleCount - 1, rangeSlot - emptySlots);
    const size_t logicalIndex = state_.count - sampleCount + sampleOffset;
    return state_.samples[physicalIndexFromOldest(logicalIndex)].voltageCenti;
}

void PersistentHistory::reset()
{
    state_ = {};
    state_.magic = MAGIC;
    state_.version = VERSION;
}

bool PersistentHistory::load()
{
    PersistedMetadata metadata = {};
    if (preferences_.getBytes(METADATA_KEY, &metadata, sizeof(metadata)) != sizeof(metadata) ||
        metadata.magic != MAGIC || metadata.version != VERSION ||
        metadata.next >= HISTORY_CAPACITY || metadata.count > HISTORY_CAPACITY ||
        (metadata.count < HISTORY_CAPACITY && metadata.next != metadata.count)) {
        return false;
    }

    reset();
    state_.next = metadata.next;
    state_.count = metadata.count;
    const size_t chunksToLoad = state_.count == 0
        ? 0
        : (state_.count + SAMPLES_PER_CHUNK - 1) / SAMPLES_PER_CHUNK;
    for (size_t chunk = 0; chunk < chunksToLoad; ++chunk) {
        char key[8];
        std::snprintf(key, sizeof(key), "c%02u", static_cast<unsigned>(chunk));
        const size_t sampleCount = samplesInChunk(chunk);
        if (preferences_.getBytes(key, &state_.samples[chunk * SAMPLES_PER_CHUNK],
                                  sampleCount * sizeof(HistorySample)) !=
            sampleCount * sizeof(HistorySample)) {
            return false;
        }
    }
    return true;
}

bool PersistentHistory::saveMetadata()
{
    PersistedMetadata metadata = {
        state_.magic,
        state_.version,
        state_.reserved,
        state_.next,
        state_.count
    };
    return preferences_.putBytes(METADATA_KEY, &metadata, sizeof(metadata)) == sizeof(metadata);
}

bool PersistentHistory::saveChunk(size_t sampleIndex)
{
    const size_t chunk = sampleIndex / SAMPLES_PER_CHUNK;
    char key[8];
    std::snprintf(key, sizeof(key), "c%02u", static_cast<unsigned>(chunk));
    const size_t sampleCount = samplesInChunk(chunk);
    return preferences_.putBytes(key, &state_.samples[chunk * SAMPLES_PER_CHUNK],
                                 sampleCount * sizeof(HistorySample)) ==
           sampleCount * sizeof(HistorySample);
}

size_t PersistentHistory::samplesInChunk(size_t chunkIndex) const
{
    const size_t first = chunkIndex * SAMPLES_PER_CHUNK;
    return std::min(SAMPLES_PER_CHUNK, HISTORY_CAPACITY - first);
}

size_t PersistentHistory::recentSampleCount(size_t requested) const
{
    return std::min(static_cast<size_t>(state_.count), requested);
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
