#include <Arduino.h>
#include <NimBLEDevice.h>

namespace {
class ScanCallbacks : public NimBLEScanCallbacks {
  public:
    void onResult(const NimBLEAdvertisedDevice *device) override
    {
        Serial.printf("%s RSSI %d", device->getAddress().toString().c_str(), device->getRSSI());
        if (device->haveName()) {
            Serial.printf(" name=\"%s\"", device->getName().c_str());
        }
        if (device->haveServiceUUID()) {
            Serial.print(" services=");
            for (uint8_t i = 0; i < device->getServiceUUIDCount(); ++i) {
                Serial.print(device->getServiceUUID(i).toString().c_str());
                Serial.print(" ");
            }
        }
        Serial.println();
    }

    void onScanEnd(const NimBLEScanResults &, int reason) override
    {
        Serial.printf("Scan ended, reason %d\n", reason);
    }
};

ScanCallbacks callbacks;
} // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("BLE scan debug starting");
    NimBLEDevice::init("bm6-scan-debug");
    NimBLEDevice::setPower(3);
}

void loop()
{
    Serial.println("Scanning BLE for 15 seconds...");
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&callbacks, false);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(100);
    scan->start(15000, false, true);
    while (scan->isScanning()) {
        delay(100);
    }
    Serial.println("Scan cycle complete");
    delay(5000);
}
