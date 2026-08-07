#pragma once

#include <Arduino.h>
#include <Preferences.h>

static constexpr uint8_t MAX_SAVED_BM6_DEVICES = 4;

struct SavedBm6Device {
    char address[18] = "";
    char name[16] = "";
    int8_t lastRssi = -127;
    uint8_t addressType = 1;
    uint8_t historySlot = 0;
    uint8_t reserved = 0;
};

class SavedBm6Registry {
  public:
    bool begin();
    uint8_t count() const;
    uint8_t activeIndex() const;
    const SavedBm6Device *active() const;
    const SavedBm6Device *device(uint8_t index) const;
    int8_t addOrSelect(const char *address, uint8_t addressType, const char *advertisedName, int rssi);
    bool select(uint8_t index);
    bool selectRelative(int8_t direction);
    bool rename(uint8_t index, const char *name);
    bool updateRssi(uint8_t index, int rssi);

  private:
    struct PersistedState {
        uint32_t magic;
        uint16_t version;
        uint8_t count;
        uint8_t activeIndex;
        SavedBm6Device devices[MAX_SAVED_BM6_DEVICES];
    };

    static constexpr uint32_t MAGIC = 0x42364452;
    static constexpr uint16_t VERSION = 1;

    Preferences preferences_;
    PersistedState state_ = {};

    void reset();
    bool load();
    bool migrateLegacySelection();
    bool save();
};
