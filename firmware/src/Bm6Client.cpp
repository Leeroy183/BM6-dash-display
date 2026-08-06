#include "Bm6Client.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <mbedtls/aes.h>
#include "config.h"

namespace {
constexpr char SERVICE_UUID[] = "0000fff0-0000-1000-8000-00805f9b34fb";
constexpr char WRITE_UUID[] = "0000fff3-0000-1000-8000-00805f9b34fb";
constexpr char NOTIFY_UUID[] = "0000fff4-0000-1000-8000-00805f9b34fb";

constexpr uint8_t AES_KEY[16] = {
    108, 101, 97, 103, 101, 110, 100, 255,
    254, 48, 49, 48, 48, 48, 48, 57
};

constexpr uint8_t READ_COMMAND[16] = {
    0x69, 0x7e, 0xc5, 0x89, 0x20, 0x17, 0xe9, 0xfe,
    0xab, 0x1c, 0x0a, 0xf6, 0xfd, 0xe8, 0x14, 0x14
};

bool hasAddress(const char *address)
{
    return address[0] != '\0' && std::strcmp(address, "00:00:00:00:00:00") != 0;
}

std::string lowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}
} // namespace

class Bm6Client::ScanCallbacks : public NimBLEScanCallbacks {
  public:
    explicit ScanCallbacks(Bm6Client &client) : client_(client) {}

    void onResult(const NimBLEAdvertisedDevice *device) override
    {
        const std::string address = lowerCopy(device->getAddress().toString());
        const char *target = hasAddress(client_.preferredAddress_) ? client_.preferredAddress_ : BM6_MAC_ADDRESS;
        const bool targetConfigured = hasAddress(target);
        const std::string configured = lowerCopy(target);
        const bool addressMatches = targetConfigured && address == configured;
        const bool nameMatches = !targetConfigured && device->haveName() &&
                                 device->getName() == BM6_ADVERTISED_NAME;

        if (!addressMatches && !nameMatches) {
            return;
        }

        client_.foundAddress_ = device->getAddress();
        client_.foundRssi_ = device->getRSSI();
        client_.found_ = true;
        std::strncpy(client_.lastAddress_, device->getAddress().toString().c_str(), sizeof(client_.lastAddress_) - 1);
        client_.lastAddress_[sizeof(client_.lastAddress_) - 1] = '\0';
        device->getScan()->stop();
    }

  private:
    Bm6Client &client_;
};

void Bm6Client::begin()
{
    if (bleStarted_) {
        return;
    }

    NimBLEDevice::init("bm6-dash");
    NimBLEDevice::setPower(3);
    bleStarted_ = true;
}

Bm6PollResult Bm6Client::poll(BatteryReading &reading)
{
    begin();

    NimBLEAddress address;
    int rssi = 0;
    if (!findDevice(address, rssi)) {
        return Bm6PollResult::NotFound;
    }

    return pollResolvedAddress(address, rssi, reading);
}

Bm6PollResult Bm6Client::pollAddress(const char *address, uint8_t addressType, BatteryReading &reading)
{
    if (!hasAddress(address)) {
        return Bm6PollResult::NotFound;
    }

    begin();
    NimBLEAddress target(std::string(address), addressType);
    return pollResolvedAddress(target, 0, reading);
}

void Bm6Client::setPreferredAddress(const char *address, uint8_t addressType)
{
    if (!hasAddress(address)) {
        preferredAddress_[0] = '\0';
        preferredAddressType_ = 1;
        return;
    }

    std::strncpy(preferredAddress_, address, sizeof(preferredAddress_) - 1);
    preferredAddress_[sizeof(preferredAddress_) - 1] = '\0';
    preferredAddressType_ = addressType;
}

const char *Bm6Client::preferredAddress() const
{
    return preferredAddress_;
}

uint8_t Bm6Client::preferredAddressType() const
{
    return preferredAddressType_;
}

Bm6PollResult Bm6Client::pollResolvedAddress(const NimBLEAddress &address, int rssi, BatteryReading &reading)
{
    NimBLEClient *client = NimBLEDevice::createClient();
    if (client == nullptr) {
        return Bm6PollResult::ConnectFailed;
    }

    client->setConnectTimeout(5000);
    Bm6PollResult result = Bm6PollResult::Ok;

    if (!client->connect(address)) {
        result = Bm6PollResult::ConnectFailed;
    } else {
        NimBLERemoteService *service = client->getService(SERVICE_UUID);
        if (service == nullptr) {
            result = Bm6PollResult::ServiceMissing;
        } else {
            NimBLERemoteCharacteristic *writer = service->getCharacteristic(WRITE_UUID);
            NimBLERemoteCharacteristic *notifier = service->getCharacteristic(NOTIFY_UUID);
            if (writer == nullptr || notifier == nullptr) {
                result = Bm6PollResult::CharacteristicMissing;
            } else {
                packetReady_ = false;

                const bool subscribed = notifier->subscribe(true, [this](
                    NimBLERemoteCharacteristic *, uint8_t *data, size_t length, bool
                ) {
                    handleNotification(data, length);
                });

                if (!subscribed) {
                    result = Bm6PollResult::SubscribeFailed;
                } else if (!writer->writeValue(READ_COMMAND, sizeof(READ_COMMAND), false)) {
                    result = Bm6PollResult::WriteFailed;
                } else {
                    const uint32_t started = millis();
                    while (!packetReady_ && millis() - started < BM6_PACKET_TIMEOUT_MS) {
                        delay(20);
                    }

                    if (packetReady_) {
                        if (packetToReading(encryptedPacket_, sizeof(encryptedPacket_), reading)) {
                            reading.rssi = rssi;
                            reading.sampledAtMs = millis();
                            result = Bm6PollResult::Ok;
                        } else {
                            result = Bm6PollResult::InvalidPacket;
                        }
                    } else {
                        result = Bm6PollResult::Timeout;
                    }

                    notifier->unsubscribe(false);
                }
            }
        }
    }

    if (client->isConnected()) {
        client->disconnect();
    }
    if (result == Bm6PollResult::Ok) {
        std::strncpy(lastAddress_, address.toString().c_str(), sizeof(lastAddress_) - 1);
        lastAddress_[sizeof(lastAddress_) - 1] = '\0';
    }
    NimBLEDevice::deleteClient(client);
    return result;
}

const char *Bm6Client::lastDeviceAddress() const
{
    return lastAddress_;
}

int Bm6Client::lastRssi() const
{
    return foundRssi_;
}

bool Bm6Client::findDevice(NimBLEAddress &address, int &rssi)
{
    found_ = false;
    foundRssi_ = 0;

    NimBLEScan *scan = NimBLEDevice::getScan();
    ScanCallbacks callbacks(*this);
    scan->setScanCallbacks(&callbacks, false);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(100);

    if (!scan->start(BM6_SCAN_TIMEOUT_MS, false, true)) {
        scan->setScanCallbacks(nullptr, false);
        return false;
    }

    const uint32_t started = millis();
    while (!found_ && scan->isScanning() && millis() - started < BM6_SCAN_TIMEOUT_MS + 250) {
        delay(25);
    }
    if (scan->isScanning()) {
        scan->stop();
    }
    scan->setScanCallbacks(nullptr, false);

    if (!found_) {
        return false;
    }

    address = foundAddress_;
    rssi = foundRssi_;
    return true;
}

bool Bm6Client::packetToReading(const uint8_t *encrypted, size_t length, BatteryReading &reading) const
{
    if (length != 16) {
        return false;
    }

    uint8_t iv[16] = {};
    uint8_t decrypted[16] = {};
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    const int keyResult = mbedtls_aes_setkey_dec(&aes, AES_KEY, 128);
    const int decryptResult = keyResult == 0
        ? mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, length, iv, encrypted, decrypted)
        : keyResult;
    mbedtls_aes_free(&aes);

    if (decryptResult != 0) {
        return false;
    }

    if (decrypted[0] != 0xd1 || decrypted[1] != 0x55 || decrypted[2] != 0x07) {
        return false;
    }

    reading.temperatureC = decrypted[3] == 1 ? -static_cast<int>(decrypted[4]) : static_cast<int>(decrypted[4]);
    reading.socPercent = decrypted[6];
    reading.voltage = static_cast<float>((static_cast<uint16_t>(decrypted[7]) << 8) | decrypted[8]) / 100.0f;
    return reading.voltage > 0.0f && reading.socPercent <= 100;
}

void Bm6Client::handleNotification(uint8_t *data, size_t length)
{
    if (length != sizeof(encryptedPacket_)) {
        return;
    }

    std::memcpy(encryptedPacket_, data, sizeof(encryptedPacket_));
    packetReady_ = true;
}
