#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "BatteryTypes.h"

enum class Bm6PollResult {
    Ok,
    NotFound,
    ConnectFailed,
    ServiceMissing,
    CharacteristicMissing,
    SubscribeFailed,
    WriteFailed,
    Timeout,
    InvalidPacket
};

class Bm6Client {
  public:
    void begin();
    Bm6PollResult poll(BatteryReading &reading);
    Bm6PollResult pollAddress(const char *address, uint8_t addressType, int rssi, BatteryReading &reading);
    void setPreferredAddress(const char *address, uint8_t addressType);
    const char *preferredAddress() const;
    uint8_t preferredAddressType() const;
    const char *lastDeviceAddress() const;
    int lastRssi() const;

  private:
    class ScanCallbacks;

    bool findDevice(NimBLEAddress &address, int &rssi);
    Bm6PollResult pollResolvedAddress(const NimBLEAddress &address, int rssi, BatteryReading &reading);
    bool packetToReading(const uint8_t *encrypted, size_t length, BatteryReading &reading) const;
    void handleNotification(uint8_t *data, size_t length);

    bool bleStarted_ = false;
    volatile bool found_ = false;
    volatile bool packetReady_ = false;
    NimBLEAddress foundAddress_;
    int foundRssi_ = 0;
    char lastAddress_[18] = "";
    char preferredAddress_[18] = "";
    uint8_t preferredAddressType_ = 1;
    uint8_t encryptedPacket_[16] = {};
};
