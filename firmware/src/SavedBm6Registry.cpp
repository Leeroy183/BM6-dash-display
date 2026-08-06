#include "SavedBm6Registry.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <strings.h>

namespace {
constexpr char REGISTRY_NAMESPACE[] = "bm6cfg";
constexpr char REGISTRY_KEY[] = "devices";

bool validAddress(const char *address)
{
    return address != nullptr && std::strlen(address) == 17;
}
}

bool SavedBm6Registry::begin()
{
    if (!preferences_.begin(REGISTRY_NAMESPACE, false)) {
        reset();
        return false;
    }

    if (load()) {
        return true;
    }

    reset();
    migrateLegacySelection();
    return save();
}

uint8_t SavedBm6Registry::count() const
{
    return state_.count;
}

uint8_t SavedBm6Registry::activeIndex() const
{
    return state_.activeIndex;
}

const SavedBm6Device *SavedBm6Registry::active() const
{
    return device(state_.activeIndex);
}

const SavedBm6Device *SavedBm6Registry::device(uint8_t index) const
{
    return index < state_.count ? &state_.devices[index] : nullptr;
}

int8_t SavedBm6Registry::addOrSelect(
    const char *address, uint8_t addressType, const char *advertisedName, int rssi
)
{
    if (!validAddress(address)) {
        return -1;
    }

    uint8_t index = 0;
    for (; index < state_.count; ++index) {
        if (strcasecmp(state_.devices[index].address, address) == 0) {
            break;
        }
    }

    if (index == state_.count) {
        if (state_.count >= MAX_SAVED_BM6_DEVICES) {
            return -1;
        }
        ++state_.count;
        state_.devices[index] = {};
        state_.devices[index].historySlot = index;
        std::snprintf(state_.devices[index].name, sizeof(state_.devices[index].name),
                      "Battery %u", static_cast<unsigned>(index + 1));
    }

    SavedBm6Device &stored = state_.devices[index];
    std::strncpy(stored.address, address, sizeof(stored.address) - 1);
    stored.address[sizeof(stored.address) - 1] = '\0';
    stored.addressType = addressType;
    stored.lastRssi = static_cast<int8_t>(std::max(-127, std::min(20, rssi)));
    if (advertisedName != nullptr && advertisedName[0] != '\0' &&
        strcasecmp(advertisedName, "BM6") != 0) {
        std::strncpy(stored.name, advertisedName, sizeof(stored.name) - 1);
        stored.name[sizeof(stored.name) - 1] = '\0';
    }

    state_.activeIndex = index;
    return save() ? static_cast<int8_t>(index) : -1;
}

bool SavedBm6Registry::select(uint8_t index)
{
    if (index >= state_.count) {
        return false;
    }
    state_.activeIndex = index;
    return save();
}

bool SavedBm6Registry::selectRelative(int8_t direction)
{
    if (state_.count < 2 || direction == 0) {
        return false;
    }
    const int next = (static_cast<int>(state_.activeIndex) + direction + state_.count) % state_.count;
    return select(static_cast<uint8_t>(next));
}

bool SavedBm6Registry::updateRssi(uint8_t index, int rssi)
{
    if (index >= state_.count) {
        return false;
    }
    const int8_t clamped = static_cast<int8_t>(std::max(-127, std::min(20, rssi)));
    if (state_.devices[index].lastRssi == clamped) {
        return true;
    }
    state_.devices[index].lastRssi = clamped;
    return true;
}

void SavedBm6Registry::reset()
{
    state_ = {};
    state_.magic = MAGIC;
    state_.version = VERSION;
}

bool SavedBm6Registry::load()
{
    PersistedState loaded = {};
    if (preferences_.getBytes(REGISTRY_KEY, &loaded, sizeof(loaded)) != sizeof(loaded) ||
        loaded.magic != MAGIC || loaded.version != VERSION ||
        loaded.count > MAX_SAVED_BM6_DEVICES ||
        (loaded.count > 0 && loaded.activeIndex >= loaded.count)) {
        return false;
    }
    for (uint8_t i = 0; i < loaded.count; ++i) {
        if (!validAddress(loaded.devices[i].address) ||
            loaded.devices[i].historySlot >= MAX_SAVED_BM6_DEVICES) {
            return false;
        }
    }
    state_ = loaded;
    return true;
}

bool SavedBm6Registry::migrateLegacySelection()
{
    const String address = preferences_.getString("addr", "");
    if (address.length() != 17) {
        return false;
    }

    SavedBm6Device &device = state_.devices[0];
    std::strncpy(device.address, address.c_str(), sizeof(device.address) - 1);
    std::strncpy(device.name, "Battery 1", sizeof(device.name) - 1);
    device.addressType = preferences_.getUChar("atype", 1);
    device.historySlot = 0;
    state_.count = 1;
    state_.activeIndex = 0;
    Serial.printf("Migrated saved BM6 %s type %u\n", device.address, device.addressType);
    return true;
}

bool SavedBm6Registry::save()
{
    return preferences_.putBytes(REGISTRY_KEY, &state_, sizeof(state_)) == sizeof(state_);
}
