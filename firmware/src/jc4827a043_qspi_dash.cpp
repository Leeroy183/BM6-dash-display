#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <NimBLEDevice.h>
#include <Wire.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include "Bm6Client.h"
#include "PersistentHistory.h"
#include "config.h"

namespace {
constexpr int GFX_BL = 1;
constexpr int TOUCH_SDA = 8;
constexpr int TOUCH_SCL = 4;
constexpr int TOUCH_RST = 38;
constexpr int TOUCH_INT = 3;
constexpr int SCREEN_WIDTH = 480;
constexpr int SCREEN_HEIGHT = 272;
constexpr uint16_t COLOR_BLACK = 0x0000;
constexpr uint16_t COLOR_WHITE = 0xffff;
constexpr uint16_t COLOR_PANEL = 0x2104;
constexpr uint16_t COLOR_MUTED = 0x9cf3;
constexpr uint16_t COLOR_GREEN = 0x07e0;
constexpr uint16_t COLOR_AMBER = 0xfd20;
constexpr uint16_t COLOR_RED = 0xf800;
constexpr uint16_t COLOR_BLUE = 0x2d7f;
constexpr uint16_t COLOR_CYAN = 0x07ff;
constexpr uint8_t GT911_ADDR_1 = 0x5d;
constexpr uint8_t GT911_ADDR_2 = 0x14;
constexpr uint16_t GT911_POINT_STATUS = 0x814e;
constexpr uint8_t MAX_SCAN_DEVICES = 10;

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    45 /* CS */, 47 /* SCK */, 21 /* D0 */, 48 /* D1 */, 40 /* D2 */, 39 /* D3 */
);
Arduino_GFX *gfx = new Arduino_NV3041A(bus, GFX_NOT_DEFINED /* RST */, 0 /* rotation */, true /* IPS */);

struct TouchPoint {
    int16_t x = 0;
    int16_t y = 0;
};

class Gt911Touch {
  public:
    void begin()
    {
        pinMode(TOUCH_INT, INPUT);
        pinMode(TOUCH_RST, OUTPUT);
        digitalWrite(TOUCH_RST, LOW);
        delay(20);
        digitalWrite(TOUCH_RST, HIGH);
        delay(80);
        Wire.begin(TOUCH_SDA, TOUCH_SCL);
        Wire.setClock(400000);
        if (probe(GT911_ADDR_1)) {
            address_ = GT911_ADDR_1;
        } else if (probe(GT911_ADDR_2)) {
            address_ = GT911_ADDR_2;
        }
        if (address_ != 0) {
            Serial.printf("GT911 touch found at 0x%02x\n", address_);
        } else {
            Serial.println("GT911 touch not found");
        }
    }

    bool read(TouchPoint &point)
    {
        if (address_ == 0) {
            return false;
        }

        uint8_t status = 0;
        if (!readBytes(GT911_POINT_STATUS, &status, 1) || (status & 0x80) == 0) {
            return false;
        }

        const uint8_t points = status & 0x0f;
        if (points == 0) {
            clearStatus();
            return false;
        }

        uint8_t data[4] = {};
        if (!readBytes(0x814f, data, sizeof(data))) {
            clearStatus();
            return false;
        }
        clearStatus();

        const int16_t rawX = static_cast<int16_t>(data[0] | (data[1] << 8));
        const int16_t rawY = static_cast<int16_t>(data[2] | (data[3] << 8));
        point.x = constrain(SCREEN_WIDTH - 1 - rawX, 0, SCREEN_WIDTH - 1);
        point.y = constrain(SCREEN_HEIGHT - 1 - rawY, 0, SCREEN_HEIGHT - 1);
        return true;
    }

  private:
    uint8_t address_ = 0;

    bool probe(uint8_t address)
    {
        Wire.beginTransmission(address);
        return Wire.endTransmission() == 0;
    }

    bool writeRegister(uint16_t reg, uint8_t value)
    {
        Wire.beginTransmission(address_);
        Wire.write(static_cast<uint8_t>(reg >> 8));
        Wire.write(static_cast<uint8_t>(reg & 0xff));
        Wire.write(value);
        return Wire.endTransmission() == 0;
    }

    bool readBytes(uint16_t reg, uint8_t *buffer, size_t length)
    {
        Wire.beginTransmission(address_);
        Wire.write(static_cast<uint8_t>(reg >> 8));
        Wire.write(static_cast<uint8_t>(reg & 0xff));
        if (Wire.endTransmission(false) != 0) {
            return false;
        }
        return Wire.requestFrom(address_, static_cast<uint8_t>(length)) == length &&
               Wire.readBytes(buffer, length) == length;
    }

    void clearStatus()
    {
        writeRegister(GT911_POINT_STATUS, 0);
    }
};

struct BleScanDevice {
    char address[18] = "";
    char name[24] = "";
    int rssi = -127;
    bool bm6Service = false;
};

enum class Screen {
    Dash,
    Settings
};

Bm6Client bm6;
PersistentHistory history;
Gt911Touch touch;
BatteryReading latestReading;
BleScanDevice scanDevices[MAX_SCAN_DEVICES];
portMUX_TYPE scanDevicesMux = portMUX_INITIALIZER_UNLOCKED;
uint8_t scanDeviceCount = 0;
bool haveReading = false;
bool touchWasDown = false;
volatile bool settingsScanActive = false;
volatile bool scanResultsDirty = false;
uint32_t settingsScanStartedAtMs = 0;
uint32_t lastScanDrawMs = 0;
uint32_t nextPollAtMs = 0;
uint32_t lastUiTickMs = 0;
uint32_t lastChartDrawMs = 0;
char currentStatus[36] = "Starting";
Screen currentScreen = Screen::Dash;

class SettingsScanCallbacks : public NimBLEScanCallbacks {
  public:
    void onResult(const NimBLEAdvertisedDevice *device) override
    {
        const std::string addressText = device->getAddress().toString();
        BleScanDevice loggedDevice;
        portENTER_CRITICAL(&scanDevicesMux);
        BleScanDevice *slot = nullptr;
        for (uint8_t i = 0; i < scanDeviceCount; ++i) {
            if (addressText == scanDevices[i].address) {
                slot = &scanDevices[i];
                break;
            }
        }
        if (slot == nullptr) {
            if (scanDeviceCount >= MAX_SCAN_DEVICES) {
                portEXIT_CRITICAL(&scanDevicesMux);
                return;
            }
            slot = &scanDevices[scanDeviceCount++];
            std::strncpy(slot->address, addressText.c_str(), sizeof(slot->address) - 1);
            slot->address[sizeof(slot->address) - 1] = '\0';
        }

        slot->rssi = device->getRSSI();
        slot->bm6Service = device->isAdvertisingService(NimBLEUUID("0000fff0-0000-1000-8000-00805f9b34fb"));
        if (device->haveName()) {
            std::strncpy(slot->name, device->getName().c_str(), sizeof(slot->name) - 1);
            slot->name[sizeof(slot->name) - 1] = '\0';
        }
        scanResultsDirty = true;
        loggedDevice = *slot;
        portEXIT_CRITICAL(&scanDevicesMux);
        Serial.printf("BLE %s RSSI %d name=\"%s\"%s\n",
                      loggedDevice.address, loggedDevice.rssi, loggedDevice.name,
                      loggedDevice.bm6Service ? " BM6_SERVICE" : "");
    }

    void onScanEnd(const NimBLEScanResults &, int reason) override
    {
        settingsScanActive = false;
        scanResultsDirty = true;
        Serial.printf("Settings BLE scan ended, reason %d\n", reason);
    }
};

SettingsScanCallbacks settingsScanCallbacks;

uint16_t voltageColor(float voltage)
{
    if (voltage < BATTERY_CRITICAL_VOLTS) {
        return COLOR_RED;
    }
    if (voltage < BATTERY_WARNING_VOLTS) {
        return COLOR_AMBER;
    }
    return COLOR_GREEN;
}

const char *pollResultText(Bm6PollResult result)
{
    switch (result) {
        case Bm6PollResult::Ok:
            return "BM6 connected";
        case Bm6PollResult::NotFound:
            return "BM6 not found";
        case Bm6PollResult::ConnectFailed:
            return "BLE connect failed";
        case Bm6PollResult::ServiceMissing:
            return "BM6 service missing";
        case Bm6PollResult::CharacteristicMissing:
            return "BM6 char missing";
        case Bm6PollResult::SubscribeFailed:
            return "Notify failed";
        case Bm6PollResult::WriteFailed:
            return "Read command failed";
        case Bm6PollResult::Timeout:
            return "BM6 timeout";
        case Bm6PollResult::InvalidPacket:
            return "Invalid packet";
    }
    return "Unknown";
}

void clearTextArea(int x, int y, int w, int h, uint16_t color = COLOR_BLACK)
{
    gfx->fillRect(x, y, w, h, color);
}

void drawGearIcon(int cx, int cy, uint16_t color)
{
    gfx->drawCircle(cx, cy, 10, color);
    gfx->drawCircle(cx, cy, 4, color);
    gfx->drawLine(cx - 14, cy, cx - 10, cy, color);
    gfx->drawLine(cx + 10, cy, cx + 14, cy, color);
    gfx->drawLine(cx, cy - 14, cx, cy - 10, color);
    gfx->drawLine(cx, cy + 10, cx, cy + 14, color);
    gfx->drawLine(cx - 10, cy - 10, cx - 7, cy - 7, color);
    gfx->drawLine(cx + 7, cy + 7, cx + 10, cy + 10, color);
    gfx->drawLine(cx + 10, cy - 10, cx + 7, cy - 7, color);
    gfx->drawLine(cx - 7, cy + 7, cx - 10, cy + 10, color);
}

void drawCogButton()
{
    gfx->drawRoundRect(438, 4, 34, 34, 6, COLOR_MUTED);
    drawGearIcon(455, 21, COLOR_WHITE);
}

void drawStaticLayout()
{
    gfx->fillScreen(COLOR_BLACK);
    gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
    gfx->setTextSize(2);
    gfx->setCursor(14, 12);
    gfx->print("BM6 Dash Display");
    drawCogButton();

    gfx->drawRoundRect(12, 42, 456, 104, 8, COLOR_MUTED);
    gfx->drawRoundRect(12, 158, 456, 92, 8, COLOR_MUTED);
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_MUTED, COLOR_BLACK);
    gfx->setCursor(24, 52);
    gfx->print("BATTERY");
    gfx->setCursor(24, 168);
    gfx->print("3 DAY VOLTAGE HISTORY");
}

void drawStatus()
{
    clearTextArea(250, 10, 184, 18);
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_MUTED, COLOR_BLACK);
    gfx->setCursor(250, 14);
    gfx->print(currentStatus);
}

void drawNoReading()
{
    clearTextArea(24, 70, 420, 54);
    gfx->setTextSize(3);
    gfx->setTextColor(COLOR_MUTED, COLOR_BLACK);
    gfx->setCursor(24, 78);
    gfx->print("--.-- V");
}

void drawLatestReading()
{
    if (!haveReading) {
        drawNoReading();
        return;
    }

    clearTextArea(24, 70, 210, 50);
    gfx->setTextSize(4);
    gfx->setTextColor(voltageColor(latestReading.voltage), COLOR_BLACK);
    gfx->setCursor(24, 72);
    gfx->printf("%.2fV", latestReading.voltage);

    clearTextArea(250, 62, 190, 72);
    gfx->setTextSize(2);
    gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
    gfx->setCursor(250, 68);
    gfx->printf("SOC %u%%", latestReading.socPercent);
    gfx->setCursor(250, 96);
    gfx->printf("TEMP %dC", latestReading.temperatureC);
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_MUTED, COLOR_BLACK);
    gfx->setCursor(250, 122);
    gfx->printf("RSSI %d dBm", latestReading.rssi);
}

void drawFooter()
{
    clearTextArea(18, 252, 448, 16);
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_MUTED, COLOR_BLACK);
    gfx->setCursor(18, 256);
    const uint32_t remainingMs = static_cast<int32_t>(nextPollAtMs - millis()) > 0 ? nextPollAtMs - millis() : 0;
    gfx->printf("Samples %u/%u | Next poll %lus",
                static_cast<unsigned>(history.size()), static_cast<unsigned>(HISTORY_CAPACITY),
                static_cast<unsigned long>((remainingMs + 999) / 1000));
}

void drawHistoryChart()
{
    constexpr int x0 = 24;
    constexpr int y0 = 184;
    constexpr int w = 432;
    constexpr int h = 48;
    clearTextArea(x0, y0 - 8, w, h + 18, COLOR_BLACK);
    gfx->drawRect(x0, y0, w, h, COLOR_MUTED);

    if (history.size() == 0) {
        gfx->setTextSize(1);
        gfx->setTextColor(COLOR_MUTED, COLOR_BLACK);
        gfx->setCursor(x0 + 12, y0 + 18);
        gfx->print("Waiting for first history sample");
        return;
    }

    const int minCenti = std::max(1050, static_cast<int>(std::floor((history.minVoltage() - 0.15f) * 100.0f)));
    const int maxCenti = std::max(minCenti + 60, std::min(1500, static_cast<int>(std::ceil((history.maxVoltage() + 0.15f) * 100.0f))));
    int lastX = -1;
    int lastY = -1;
    for (int x = 0; x < w; ++x) {
        const int value = history.voltageHundredthsForChart(x, w);
        if (value < 0) {
            continue;
        }
        const int clamped = std::max(minCenti, std::min(maxCenti, value));
        const int y = y0 + h - 1 - ((clamped - minCenti) * (h - 2) / (maxCenti - minCenti));
        if (lastX >= 0) {
            gfx->drawLine(lastX, lastY, x0 + x, y, COLOR_GREEN);
        }
        lastX = x0 + x;
        lastY = y;
    }

    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_MUTED, COLOR_BLACK);
    gfx->setCursor(x0, y0 + h + 4);
    gfx->printf("%.2f-%.2fV", minCenti / 100.0f, maxCenti / 100.0f);
    gfx->setCursor(x0 + 346, y0 + h + 4);
    gfx->print("last 3 days");
}

void redrawAll()
{
    currentScreen = Screen::Dash;
    drawStaticLayout();
    drawStatus();
    drawLatestReading();
    drawHistoryChart();
    drawFooter();
}

void setStatus(const char *status)
{
    strncpy(currentStatus, status, sizeof(currentStatus) - 1);
    currentStatus[sizeof(currentStatus) - 1] = '\0';
    if (currentScreen == Screen::Dash) {
        drawStatus();
    }
    Serial.println(currentStatus);
}

void drawSettingsStatus(const char *status)
{
    clearTextArea(12, 44, 456, 18);
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_MUTED, COLOR_BLACK);
    gfx->setCursor(18, 48);
    gfx->print(status);
}

void drawSettingsHeader()
{
    gfx->fillScreen(COLOR_BLACK);
    gfx->drawRoundRect(8, 6, 34, 30, 6, COLOR_MUTED);
    gfx->setTextSize(2);
    gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
    gfx->setCursor(17, 12);
    gfx->print("<");
    gfx->setCursor(56, 12);
    gfx->print("Settings");

    gfx->drawRoundRect(362, 6, 104, 30, 6, COLOR_BLUE);
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
    gfx->setCursor(386, 17);
    gfx->print("SCAN");
}

void drawScanResults()
{
    BleScanDevice devices[MAX_SCAN_DEVICES];
    uint8_t deviceCount = 0;
    portENTER_CRITICAL(&scanDevicesMux);
    deviceCount = scanDeviceCount;
    std::memcpy(devices, scanDevices, sizeof(devices));
    portEXIT_CRITICAL(&scanDevicesMux);

    std::sort(devices, devices + deviceCount, [](const BleScanDevice &a, const BleScanDevice &b) {
        return a.rssi > b.rssi;
    });

    clearTextArea(12, 66, 456, 198);
    gfx->setTextSize(1);
    if (deviceCount == 0) {
        gfx->setTextColor(COLOR_AMBER, COLOR_BLACK);
        gfx->setCursor(18, 92);
        gfx->print("No BLE devices found");
        return;
    }

    for (uint8_t i = 0; i < deviceCount; ++i) {
        const int y = 72 + i * 18;
        clearTextArea(18, y, 438, 12);
        gfx->setTextColor(devices[i].bm6Service ? COLOR_GREEN : COLOR_WHITE, COLOR_BLACK);
        gfx->setCursor(18, y);
        gfx->printf("%s", devices[i].address);
        gfx->setTextColor(COLOR_MUTED, COLOR_BLACK);
        gfx->setCursor(158, y);
        gfx->printf("%4d", devices[i].rssi);
        gfx->setCursor(204, y);
        const char *name = devices[i].name[0] ? devices[i].name : "(no name)";
        gfx->printf("%.22s", name);
        if (devices[i].bm6Service) {
            gfx->setTextColor(COLOR_GREEN, COLOR_BLACK);
            gfx->setCursor(390, y);
            gfx->print("BM6");
        }
    }
}

void stopSettingsScan()
{
    if (!settingsScanActive) {
        NimBLEDevice::getScan()->setMaxResults(0xff);
        return;
    }

    NimBLEScan *scan = NimBLEDevice::getScan();
    if (scan->isScanning()) {
        scan->stop();
        Serial.println("Settings BLE scan canceled");
    }
    scan->setScanCallbacks(nullptr, false);
    scan->setMaxResults(0xff);
    settingsScanActive = false;
}

void beginSettingsScan()
{
    currentScreen = Screen::Settings;
    stopSettingsScan();
    scanDeviceCount = 0;
    scanResultsDirty = false;
    drawSettingsHeader();
    drawSettingsStatus("Scanning nearby BLE devices...");
    clearTextArea(12, 66, 456, 198);
    Serial.println("Settings BLE scan starting");

    bm6.begin();
    portENTER_CRITICAL(&scanDevicesMux);
    scanDeviceCount = 0;
    std::memset(scanDevices, 0, sizeof(scanDevices));
    portEXIT_CRITICAL(&scanDevicesMux);

    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&settingsScanCallbacks, false);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(100);
    scan->setMaxResults(MAX_SCAN_DEVICES);
    settingsScanStartedAtMs = millis();
    lastScanDrawMs = 0;
    settingsScanActive = scan->start(BLE_SETTINGS_SCAN_MS, false, true);
    if (!settingsScanActive) {
        drawSettingsStatus("BLE scan failed to start");
        scan->setScanCallbacks(nullptr, false);
        Serial.println("Settings BLE scan failed to start");
    }
}

void serviceSettingsScan()
{
    if (currentScreen != Screen::Settings) {
        return;
    }

    const uint32_t now = millis();
    if (settingsScanActive && now - settingsScanStartedAtMs > BLE_SETTINGS_SCAN_MS + 1000UL) {
        settingsScanActive = false;
        NimBLEDevice::getScan()->setScanCallbacks(nullptr, false);
        NimBLEDevice::getScan()->setMaxResults(0xff);
        scanResultsDirty = true;
    }

    if (settingsScanActive && now - lastScanDrawMs >= 1000) {
        char buffer[52];
        uint8_t deviceCount = 0;
        portENTER_CRITICAL(&scanDevicesMux);
        deviceCount = scanDeviceCount;
        portEXIT_CRITICAL(&scanDevicesMux);
        const uint32_t elapsed = now - settingsScanStartedAtMs;
        const uint32_t remaining = elapsed >= BLE_SETTINGS_SCAN_MS ? 0 : (BLE_SETTINGS_SCAN_MS - elapsed + 999) / 1000;
        snprintf(buffer, sizeof(buffer), "Scanning... %u devices | %lus left",
                 static_cast<unsigned>(deviceCount), static_cast<unsigned long>(remaining));
        drawSettingsStatus(buffer);
        lastScanDrawMs = now;
    }

    if (!settingsScanActive && scanResultsDirty) {
        NimBLEDevice::getScan()->setScanCallbacks(nullptr, false);
        NimBLEDevice::getScan()->setMaxResults(0xff);
        drawSettingsStatus("Tap SCAN to rescan, Back to return");
        drawScanResults();
        scanResultsDirty = false;
        uint8_t deviceCount = 0;
        portENTER_CRITICAL(&scanDevicesMux);
        deviceCount = scanDeviceCount;
        portEXIT_CRITICAL(&scanDevicesMux);
        Serial.printf("Settings BLE scan complete, %u devices\n", deviceCount);
    } else if (settingsScanActive && scanResultsDirty && now - lastScanDrawMs >= 250) {
        drawScanResults();
        scanResultsDirty = false;
        lastScanDrawMs = now;
    }
}

void pollBm6Now()
{
    if (currentScreen != Screen::Dash) {
        return;
    }
    setStatus("Scanning BM6");
    BatteryReading reading;
    const Bm6PollResult result = bm6.poll(reading);
    setStatus(pollResultText(result));

    if (result == Bm6PollResult::Ok) {
        latestReading = reading;
        haveReading = true;
        history.addIfDue(reading);
        drawLatestReading();
        drawHistoryChart();
        Serial.printf("BM6 %.2fV %u%% %dC RSSI %d\n",
                      reading.voltage, reading.socPercent, reading.temperatureC, reading.rssi);
    }

    nextPollAtMs = millis() + BM6_POLL_INTERVAL_MS;
    drawFooter();
}

void handleTouchTap(const TouchPoint &point)
{
    Serial.printf("Touch %d,%d\n", point.x, point.y);
    if (currentScreen == Screen::Dash && point.x >= 438 && point.y <= 40) {
        beginSettingsScan();
        return;
    }
    if (currentScreen == Screen::Settings && point.x <= 48 && point.y <= 44) {
        stopSettingsScan();
        Serial.println("Settings closed");
        redrawAll();
        return;
    }
    if (currentScreen == Screen::Settings && point.x >= 352 && point.y <= 44) {
        beginSettingsScan();
    }
}

void handleTouch()
{
    TouchPoint point;
    const bool touched = touch.read(point);
    if (touched && !touchWasDown) {
        touchWasDown = true;
        handleTouchTap(point);
    } else if (!touched) {
        touchWasDown = false;
    }
}

void handleSerial()
{
    if (!Serial.available()) {
        return;
    }
    const char command = static_cast<char>(Serial.read());
    if (command == 's') {
        beginSettingsScan();
    } else if (command == 'd') {
        stopSettingsScan();
        Serial.println("Settings closed");
        redrawAll();
    } else if (command == 'r' && currentScreen == Screen::Settings) {
        beginSettingsScan();
    }
}
} // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("BM6 QSPI dash starting");

    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);

    if (!gfx->begin()) {
        Serial.println("Display begin failed");
        return;
    }
    gfx->invertDisplay(true);
    gfx->setRotation(2);
    touch.begin();

    history.begin();
    history.latest(latestReading);
    haveReading = history.size() > 0;
    redrawAll();

    bm6.begin();
    pollBm6Now();
}

void loop()
{
    const uint32_t now = millis();
    handleSerial();
    handleTouch();
    serviceSettingsScan();

    if (currentScreen == Screen::Dash && static_cast<int32_t>(now - nextPollAtMs) >= 0) {
        pollBm6Now();
    }
    if (currentScreen == Screen::Dash && now - lastUiTickMs >= 1000) {
        drawFooter();
        lastUiTickMs = now;
    }
    if (currentScreen == Screen::Dash && now - lastChartDrawMs >= 60000) {
        drawHistoryChart();
        lastChartDrawMs = now;
    }
    delay(50);
}
