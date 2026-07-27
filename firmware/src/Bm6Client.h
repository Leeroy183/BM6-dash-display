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
    const char *lastDeviceAddress() const;
    int lastRssi() const;

  private:
    class ScanCallbacks;

    bool findDevice(NimBLEAddress &address, int &rssi);
    bool packetToReading(const uint8_t *encrypted, size_t length, BatteryReading &reading) const;
    void handleNotification(uint8_t *data, size_t length);

    bool bleStarted_ = false;
    volatile bool found_ = false;
    volatile bool packetReady_ = false;
    NimBLEAddress foundAddress_;
    int foundRssi_ = 0;
    char lastAddress_[18] = "";
    uint8_t encryptedPacket_[16] = {};
};
