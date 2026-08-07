#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <NimBLEDevice.h>
#include <Wire.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <strings.h>
#include <string>
#include "Bm6Client.h"
#include "PersistentHistory.h"
#include "SavedBm6Registry.h"
#include "config.h"

namespace {
constexpr int GFX_BL = 1;
constexpr int TOUCH_SDA = 8;
constexpr int TOUCH_SCL = 4;
constexpr int TOUCH_RST = 38;
constexpr int TOUCH_INT = 3;
constexpr int SCREEN_WIDTH = 480;
constexpr int SCREEN_HEIGHT = 272;
constexpr uint8_t DISPLAY_ROTATION = 0;
constexpr uint16_t COLOR_BLACK = 0x0000;
constexpr uint16_t COLOR_BACKGROUND = COLOR_BLACK;
constexpr uint16_t COLOR_WHITE = 0xffff;
constexpr uint16_t COLOR_PANEL = COLOR_BLACK;
constexpr uint16_t COLOR_PANEL_LIGHT = COLOR_BLACK;
constexpr uint16_t COLOR_MUTED = 0x9cf3;
constexpr uint16_t COLOR_GREEN = 0x07e0;
constexpr uint16_t COLOR_AMBER = 0xfd20;
constexpr uint16_t COLOR_RED = 0xf800;
constexpr uint16_t COLOR_BLUE = 0x2d7f;
constexpr uint16_t COLOR_CYAN = 0x07ff;
constexpr uint8_t GT911_ADDR_1 = 0x5d;
constexpr uint8_t GT911_ADDR_2 = 0x14;
constexpr uint16_t GT911_POINT_STATUS = 0x814e;
constexpr uint8_t MAX_SCAN_DEVICES = 32;
constexpr uint8_t SETTINGS_ROWS_PER_PAGE = 7;

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
            clearStatus();
            uint8_t resolution[4] = {};
            if (readBytes(0x8048, resolution, sizeof(resolution))) {
                const uint16_t width = resolution[0] | (resolution[1] << 8);
                const uint16_t height = resolution[2] | (resolution[3] << 8);
                Serial.printf("GT911 configured resolution %ux%u\n", width, height);
            }
        } else {
            Serial.println("GT911 touch not found");
        }
    }

    bool read(TouchPoint &point, bool &contact)
    {
        contact = false;
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
            return true;
        }

        uint8_t data[5] = {};
        if (!readBytes(0x814f, data, sizeof(data))) {
            clearStatus();
            return false;
        }
        clearStatus();

        const int16_t rawX = static_cast<int16_t>(data[1] | (data[2] << 8));
        const int16_t rawY = static_cast<int16_t>(data[3] | (data[4] << 8));
        point.x = constrain(rawX, 0, SCREEN_WIDTH - 1);
        point.y = constrain(rawY, 0, SCREEN_HEIGHT - 1);
        contact = true;
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
    uint8_t addressType = 1;
    uint16_t firstSeenScan = 0;
    uint16_t lastSeenScan = 0;
    bool bm6Service = false;
};

enum class Screen {
    Dash,
    Settings
};

enum class DashPage : uint8_t {
    Overview,
    History,
    Tests
};

enum class HistoryRange : uint8_t {
    SixHours,
    OneDay,
    ThreeDays
};

Bm6Client bm6;
PersistentHistory history;
SavedBm6Registry registry;
Gt911Touch touch;
BatteryReading latestReading;
BleScanDevice scanDevices[MAX_SCAN_DEVICES];
portMUX_TYPE scanDevicesMux = portMUX_INITIALIZER_UNLOCKED;
uint8_t scanDeviceCount = 0;
bool haveReading = false;
bool touchWasDown = false;
uint32_t lastTouchContactAtMs = 0;
bool bm6TargetSelected = false;
volatile bool bm6PollActive = false;
volatile bool bm6PollResultReady = false;
bool settingsScanPending = false;
bool settingsShowingResults = false;
bool renameActive = false;
char renameBuffer[16] = "";
uint8_t renameLength = 0;
volatile bool settingsScanActive = false;
volatile bool scanResultsDirty = false;
uint16_t settingsScanGeneration = 0;
uint8_t settingsPage = 0;
uint32_t settingsScanStartedAtMs = 0;
uint32_t lastScanDrawMs = 0;
uint32_t nextPollAtMs = 0;
uint32_t lastUiTickMs = 0;
uint32_t lastChartDrawMs = 0;
char currentStatus[36] = "Starting";
Screen currentScreen = Screen::Dash;
DashPage dashPage = DashPage::Overview;
HistoryRange historyRange = HistoryRange::ThreeDays;
uint8_t consecutivePollFailures = 0;

struct Bm6PollJob {
    char address[18] = "";
    uint8_t addressType = 1;
    int rssi = -127;
    bool useDirectAddress = false;
};

portMUX_TYPE bm6PollMux = portMUX_INITIALIZER_UNLOCKED;
Bm6PollJob bm6PollJob;
BatteryReading bm6PendingReading;
Bm6PollResult bm6PendingResult = Bm6PollResult::NotFound;

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
                uint8_t replaceIndex = 0;
                for (uint8_t i = 1; i < scanDeviceCount; ++i) {
                    const bool iIsStale = scanDevices[i].lastSeenScan != settingsScanGeneration;
                    const bool replaceIsStale = scanDevices[replaceIndex].lastSeenScan != settingsScanGeneration;
                    if ((iIsStale && !replaceIsStale) ||
                        (iIsStale == replaceIsStale && scanDevices[i].rssi < scanDevices[replaceIndex].rssi)) {
                        replaceIndex = i;
                    }
                }
                if (scanDevices[replaceIndex].lastSeenScan == settingsScanGeneration &&
                    device->getRSSI() <= scanDevices[replaceIndex].rssi) {
                    portEXIT_CRITICAL(&scanDevicesMux);
                    return;
                }
                slot = &scanDevices[replaceIndex];
                *slot = {};
            } else {
                slot = &scanDevices[scanDeviceCount++];
            }
            std::strncpy(slot->address, addressText.c_str(), sizeof(slot->address) - 1);
            slot->address[sizeof(slot->address) - 1] = '\0';
            slot->firstSeenScan = settingsScanGeneration;
        }

        slot->rssi = device->getRSSI();
        slot->addressType = device->getAddress().getType();
        slot->lastSeenScan = settingsScanGeneration;
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

void stopSettingsScan();

bool containsInsensitive(const char *text, const char *needle)
{
    if (text == nullptr || needle == nullptr || needle[0] == '\0') {
        return false;
    }

    const size_t needleLength = std::strlen(needle);
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        size_t i = 0;
        while (i < needleLength && cursor[i] != '\0' &&
               std::tolower(static_cast<unsigned char>(cursor[i])) ==
                   std::tolower(static_cast<unsigned char>(needle[i]))) {
            ++i;
        }
        if (i == needleLength) {
            return true;
        }
    }
    return false;
}

bool shouldSkipDeviceTest(const BleScanDevice &device)
{
    return containsInsensitive(device.name, "pump") ||
           containsInsensitive(device.name, "dexcom") ||
           containsInsensitive(device.name, "dxcm") ||
           containsInsensitive(device.name, "omnipod");
}

bool hasCompileTimeBm6Address()
{
    return std::strcmp(BM6_MAC_ADDRESS, "00:00:00:00:00:00") != 0;
}

bool isNewThisScan(const BleScanDevice &device)
{
    return settingsScanGeneration > 1 && device.firstSeenScan == settingsScanGeneration;
}

int8_t savedDeviceIndex(const char *address)
{
    for (uint8_t i = 0; i < registry.count(); ++i) {
        const SavedBm6Device *saved = registry.device(i);
        if (saved != nullptr && strcasecmp(saved->address, address) == 0) {
            return static_cast<int8_t>(i);
        }
    }
    return -1;
}

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
    gfx->drawRoundRect(438, 4, 34, 34, 6, COLOR_PANEL_LIGHT);
    drawGearIcon(455, 21, COLOR_WHITE);
}

const char *activeBatteryName()
{
    const SavedBm6Device *device = registry.active();
    return device == nullptr ? "No battery" : device->name;
}

size_t historySamplesForRange()
{
    switch (historyRange) {
        case HistoryRange::SixHours:
            return 6UL * 60UL / 5UL;
        case HistoryRange::OneDay:
            return 24UL * 60UL / 5UL;
        case HistoryRange::ThreeDays:
            return HISTORY_CAPACITY;
    }
    return HISTORY_CAPACITY;
}

const char *historyRangeLabel()
{
    switch (historyRange) {
        case HistoryRange::SixHours:
            return "LAST 6 HOURS";
        case HistoryRange::OneDay:
            return "LAST 24 HOURS";
        case HistoryRange::ThreeDays:
            return "LAST 3 DAYS";
    }
    return "HISTORY";
}

void drawHeader()
{
    gfx->fillRect(0, 0, SCREEN_WIDTH, 42, COLOR_PANEL);
    const bool multiple = registry.count() > 1;
    gfx->setTextSize(2);
    gfx->setTextColor(multiple ? COLOR_WHITE : COLOR_MUTED, COLOR_PANEL);
    gfx->setCursor(10, 13);
    gfx->print(multiple ? "<" : " ");
    gfx->setCursor(211, 13);
    gfx->print(multiple ? ">" : " ");

    gfx->setTextColor(COLOR_WHITE, COLOR_PANEL);
    gfx->setCursor(36, 8);
    gfx->printf("%.15s", activeBatteryName());
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_MUTED, COLOR_PANEL);
    gfx->setCursor(38, 27);
    const SavedBm6Device *device = registry.active();
    if (device != nullptr) {
        gfx->printf("...%s  %u/%u", device->address + 12,
                    static_cast<unsigned>(registry.activeIndex() + 1),
                    static_cast<unsigned>(registry.count()));
    } else {
        gfx->print("Tap settings to add BM6");
    }
    drawCogButton();
}

void drawStatus()
{
    clearTextArea(240, 5, 194, 32, COLOR_PANEL);
    gfx->setTextSize(1);
    const uint16_t color = std::strcmp(currentStatus, "BM6 connected") == 0 ? COLOR_GREEN : COLOR_MUTED;
    gfx->fillCircle(248, 21, 3, color);
    gfx->setTextColor(color, COLOR_PANEL);
    gfx->setCursor(257, 17);
    gfx->printf("%.22s", currentStatus);
}

void drawNavigation()
{
    constexpr int y = 238;
    constexpr int tabWidth = 160;
    gfx->fillRect(0, y, SCREEN_WIDTH, SCREEN_HEIGHT - y, COLOR_PANEL);
    for (uint8_t i = 0; i < 3; ++i) {
        const bool active = static_cast<uint8_t>(dashPage) == i;
        if (active) {
            gfx->drawRoundRect(i * tabWidth + 4, y + 3, tabWidth - 8, 28, 6, COLOR_GREEN);
        }
    }

    gfx->setTextSize(1);
    gfx->setTextColor(dashPage == DashPage::Overview ? COLOR_GREEN : COLOR_MUTED,
                      dashPage == DashPage::Overview ? COLOR_PANEL_LIGHT : COLOR_PANEL);
    gfx->setCursor(56, 251);
    gfx->print("OVERVIEW");
    gfx->setTextColor(dashPage == DashPage::History ? COLOR_GREEN : COLOR_MUTED,
                      dashPage == DashPage::History ? COLOR_PANEL_LIGHT : COLOR_PANEL);
    gfx->setCursor(220, 251);
    gfx->print("HISTORY");
    gfx->setTextColor(dashPage == DashPage::Tests ? COLOR_GREEN : COLOR_MUTED,
                      dashPage == DashPage::Tests ? COLOR_PANEL_LIGHT : COLOR_PANEL);
    gfx->setCursor(382, 251);
    gfx->print("TESTS");
}

void drawChart(int x0, int y0, int w, int h, size_t recentSamples, bool grid)
{
    gfx->fillRect(x0, y0, w, h, COLOR_PANEL_LIGHT);
    gfx->drawRect(x0, y0, w, h, COLOR_MUTED);
    if (grid) {
        for (uint8_t i = 1; i < 4; ++i) {
            const int y = y0 + i * h / 4;
            gfx->drawFastHLine(x0 + 1, y, w - 2, COLOR_PANEL);
        }
        for (uint8_t i = 1; i < 6; ++i) {
            const int x = x0 + i * w / 6;
            gfx->drawFastVLine(x, y0 + 1, h - 2, COLOR_PANEL);
        }
    }

    if (history.size() == 0) {
        gfx->setTextSize(1);
        gfx->setTextColor(COLOR_MUTED, COLOR_PANEL_LIGHT);
        gfx->setCursor(x0 + 12, y0 + h / 2 - 4);
        gfx->print("WAITING FOR HISTORY");
        return;
    }

    constexpr int minCenti = 800;
    constexpr int maxCenti = 1600;
    int lastX = -1;
    int lastY = -1;
    for (int x = 0; x < w - 2; ++x) {
        const int value = history.voltageHundredthsForChart(x, w - 2, recentSamples);
        if (value < 0) {
            continue;
        }
        const int clamped = std::max(minCenti, std::min(maxCenti, value));
        const int y = y0 + h - 2 - ((clamped - minCenti) * (h - 4) / (maxCenti - minCenti));
        if (lastX >= 0) {
            gfx->drawLine(lastX, lastY, x0 + 1 + x, y, COLOR_GREEN);
        }
        lastX = x0 + 1 + x;
        lastY = y;
    }
}

void drawOverview()
{
    gfx->fillRect(0, 42, SCREEN_WIDTH, 196, COLOR_BACKGROUND);
    gfx->fillRoundRect(8, 49, 194, 180, 6, COLOR_PANEL_LIGHT);
    gfx->fillRoundRect(210, 49, 262, 76, 6, COLOR_PANEL_LIGHT);
    gfx->drawRoundRect(8, 49, 194, 180, 6, COLOR_MUTED);
    gfx->drawRoundRect(210, 49, 262, 76, 6, COLOR_MUTED);

    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_MUTED, COLOR_PANEL_LIGHT);
    gfx->setCursor(20, 60);
    gfx->print("STATE OF CHARGE");
    gfx->setTextSize(5);
    gfx->setTextColor(haveReading ? COLOR_WHITE : COLOR_MUTED, COLOR_PANEL_LIGHT);
    gfx->setCursor(31, 82);
    if (haveReading) {
        gfx->printf("%u%%", latestReading.socPercent);
    } else {
        gfx->print("--%");
    }

    gfx->drawRoundRect(20, 135, 166, 18, 4, COLOR_MUTED);
    if (haveReading) {
        const int fill = std::max(0, std::min(162, static_cast<int>(latestReading.socPercent) * 162 / 100));
        gfx->fillRoundRect(22, 137, fill, 14, 3, voltageColor(latestReading.voltage));
    }
    gfx->setTextSize(2);
    gfx->setTextColor(haveReading && latestReading.voltage >= 13.3f ? COLOR_GREEN : COLOR_MUTED,
                      COLOR_PANEL_LIGHT);
    gfx->setCursor(20, 165);
    gfx->print(haveReading && latestReading.voltage >= 13.3f ? "CHARGING" : "RESTING");
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_MUTED, COLOR_PANEL_LIGHT);
    gfx->setCursor(20, 201);
    gfx->printf("RSSI %d dBm   %u samples", haveReading ? latestReading.rssi : -127,
                static_cast<unsigned>(history.size()));

    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_MUTED, COLOR_PANEL_LIGHT);
    gfx->setCursor(224, 59);
    gfx->print("VOLTAGE");
    gfx->setTextSize(4);
    gfx->setTextColor(haveReading ? voltageColor(latestReading.voltage) : COLOR_MUTED,
                      COLOR_PANEL_LIGHT);
    gfx->setCursor(224, 76);
    if (haveReading) {
        gfx->printf("%.2fV", latestReading.voltage);
    } else {
        gfx->print("--.--V");
    }
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_MUTED, COLOR_PANEL_LIGHT);
    gfx->setCursor(386, 59);
    gfx->print("TEMP");
    gfx->setTextSize(2);
    gfx->setTextColor(COLOR_CYAN, COLOR_PANEL_LIGHT);
    gfx->setCursor(382, 84);
    if (haveReading) {
        gfx->printf("%dC", latestReading.temperatureC);
    } else {
        gfx->print("--C");
    }

    drawChart(210, 133, 262, 96, 6UL * 60UL / 5UL, true);
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_MUTED, COLOR_PANEL_LIGHT);
    gfx->setCursor(220, 139);
    gfx->print("6 HOUR VOLTAGE");
}

void drawHistoryPage()
{
    gfx->fillRect(0, 42, SCREEN_WIDTH, 196, COLOR_BACKGROUND);
    const HistoryRange ranges[] = {HistoryRange::SixHours, HistoryRange::OneDay, HistoryRange::ThreeDays};
    const char *labels[] = {"6H", "24H", "3D"};
    for (uint8_t i = 0; i < 3; ++i) {
        const int x = 14 + i * 70;
        const bool active = historyRange == ranges[i];
        gfx->fillRoundRect(x, 49, 62, 26, 5, active ? COLOR_GREEN : COLOR_PANEL_LIGHT);
        gfx->setTextSize(1);
        gfx->setTextColor(active ? COLOR_BLACK : COLOR_WHITE, active ? COLOR_GREEN : COLOR_PANEL_LIGHT);
        gfx->setCursor(x + 23, 59);
        gfx->print(labels[i]);
    }
    gfx->setTextColor(COLOR_MUTED, COLOR_BACKGROUND);
    gfx->setCursor(250, 59);
    gfx->printf("%s  %u/%u", historyRangeLabel(), static_cast<unsigned>(history.size()),
                static_cast<unsigned>(HISTORY_CAPACITY));

    const size_t samples = historySamplesForRange();
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_MUTED, COLOR_BACKGROUND);
    gfx->setCursor(14, 84);
    gfx->print("16V");
    gfx->setCursor(14, 144);
    gfx->print("12V");
    gfx->setCursor(14, 202);
    gfx->print("8V");
    drawChart(42, 83, 424, 128, samples, true);
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_MUTED, COLOR_BACKGROUND);
    gfx->setCursor(18, 219);
    if (history.size() > 0) {
        gfx->printf("MIN %.2fV", history.minVoltage(samples));
        gfx->setCursor(180, 219);
        gfx->printf("MAX %.2fV", history.maxVoltage(samples));
        gfx->setCursor(350, 219);
        gfx->printf("NOW %.2fV", latestReading.voltage);
    } else {
        gfx->print("NO HISTORY SAMPLES");
    }
}

void drawTestsPage()
{
    gfx->fillRect(0, 42, SCREEN_WIDTH, 196, COLOR_BACKGROUND);
    const int cardY[] = {50, 112, 174};
    const char *titles[] = {"CRANKING", "CHARGING SYSTEM", "DIODE RIPPLE"};
    for (uint8_t i = 0; i < 3; ++i) {
        gfx->fillRoundRect(12, cardY[i], 456, 54, 6, COLOR_PANEL_LIGHT);
        gfx->drawRoundRect(12, cardY[i], 456, 54, 6, COLOR_MUTED);
        gfx->fillRect(12, cardY[i], 4, 54, i == 1 ? COLOR_GREEN : COLOR_BLUE);
        gfx->setTextSize(1);
        gfx->setTextColor(COLOR_MUTED, COLOR_PANEL_LIGHT);
        gfx->setCursor(26, cardY[i] + 10);
        gfx->print(titles[i]);
    }

    gfx->setTextSize(2);
    gfx->setTextColor(COLOR_MUTED, COLOR_PANEL_LIGHT);
    gfx->setCursor(26, 77);
    gfx->print("--.--V   ---ms");
    gfx->setTextSize(1);
    gfx->setCursor(374, 78);
    gfx->print("NO DATA");

    const char *chargingState = "NO DATA";
    uint16_t chargingColor = COLOR_MUTED;
    if (haveReading) {
        if (latestReading.voltage > 14.8f) {
            chargingState = "HIGH";
            chargingColor = COLOR_RED;
        } else if (latestReading.voltage >= 13.3f) {
            chargingState = "CHARGING";
            chargingColor = COLOR_GREEN;
        } else {
            chargingState = "ENGINE OFF";
        }
    }
    gfx->setTextSize(3);
    gfx->setTextColor(chargingColor, COLOR_PANEL_LIGHT);
    gfx->setCursor(26, 136);
    if (haveReading) {
        gfx->printf("%.2fV", latestReading.voltage);
    } else {
        gfx->print("--.--V");
    }
    gfx->setTextSize(1);
    gfx->setCursor(360, 140);
    gfx->print(chargingState);

    gfx->setTextSize(2);
    gfx->setTextColor(COLOR_MUTED, COLOR_PANEL_LIGHT);
    gfx->setCursor(26, 201);
    gfx->print("---mV");
    gfx->setTextSize(1);
    gfx->setCursor(374, 202);
    gfx->print("NO DATA");
}

void drawDashboardPage()
{
    switch (dashPage) {
        case DashPage::Overview:
            drawOverview();
            break;
        case DashPage::History:
            drawHistoryPage();
            break;
        case DashPage::Tests:
            drawTestsPage();
            break;
    }
    drawNavigation();
}

void redrawDashboard()
{
    currentScreen = Screen::Dash;
    gfx->fillScreen(COLOR_BACKGROUND);
    drawHeader();
    drawStatus();
    drawDashboardPage();
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

    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_MUTED, COLOR_BLACK);
    gfx->setCursor(184, 17);
    gfx->printf("SAVED %u/%u", static_cast<unsigned>(registry.count()),
                static_cast<unsigned>(MAX_SAVED_BM6_DEVICES));

    gfx->drawRoundRect(362, 6, 104, 30, 6, COLOR_BLUE);
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
    gfx->setCursor(386, 17);
    gfx->print("SCAN");
}

void drawSavedDeviceBar()
{
    clearTextArea(12, 64, 456, 32);
    if (registry.count() == 0) {
        gfx->setTextSize(1);
        gfx->setTextColor(COLOR_MUTED, COLOR_BLACK);
        gfx->setCursor(18, 76);
        gfx->print("NO SAVED BATTERIES");
        return;
    }

    constexpr int tabsWidth = 362;
    const int width = tabsWidth / registry.count();
    for (uint8_t i = 0; i < registry.count(); ++i) {
        const SavedBm6Device *device = registry.device(i);
        const int x = 16 + i * width;
        const bool active = i == registry.activeIndex();
        gfx->fillRoundRect(x, 66, width - 6, 26, 5, active ? COLOR_GREEN : COLOR_PANEL);
        gfx->setTextSize(1);
        gfx->setTextColor(active ? COLOR_BLACK : COLOR_WHITE, active ? COLOR_GREEN : COLOR_PANEL);
        gfx->setCursor(x + 7, 76);
        gfx->printf("%u %.8s", static_cast<unsigned>(i + 1), device->name);
    }

    gfx->drawRoundRect(386, 66, 78, 26, 5, COLOR_CYAN);
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
    gfx->setCursor(404, 76);
    gfx->print("RENAME");
}

void drawSettingsHome()
{
    clearTextArea(12, 98, 456, 166);
    const SavedBm6Device *device = registry.active();
    if (device == nullptr) {
        gfx->setTextSize(2);
        gfx->setTextColor(COLOR_MUTED, COLOR_BLACK);
        gfx->setCursor(18, 116);
        gfx->print("NO SAVED BM6");
        return;
    }

    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_MUTED, COLOR_BLACK);
    gfx->setCursor(18, 110);
    gfx->print("ACTIVE BATTERY");
    gfx->setTextSize(3);
    gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
    gfx->setCursor(18, 128);
    gfx->printf("%.15s", device->name);

    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_MUTED, COLOR_BLACK);
    gfx->setCursor(18, 166);
    gfx->print("ADDRESS");
    gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
    gfx->setCursor(92, 166);
    gfx->print(device->address);
    gfx->setTextColor(COLOR_MUTED, COLOR_BLACK);
    gfx->setCursor(18, 186);
    gfx->print("LAST SIGNAL");
    gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
    gfx->setCursor(110, 186);
    gfx->printf("%d dBm", device->lastRssi);
    gfx->setTextColor(COLOR_GREEN, COLOR_BLACK);
    gfx->fillCircle(22, 220, 3, COLOR_GREEN);
    gfx->setCursor(34, 216);
    gfx->print("BACKGROUND UPDATES ACTIVE");
}

void drawKeyboardKey(int x, int y, int width, const char *label, uint16_t color = COLOR_MUTED)
{
    gfx->drawRoundRect(x, y, width, 30, 4, color);
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
    const int labelWidth = static_cast<int>(std::strlen(label)) * 6;
    gfx->setCursor(x + std::max(4, (width - labelWidth) / 2), y + 11);
    gfx->print(label);
}

void drawRenameField()
{
    gfx->fillRect(13, 41, 454, 30, COLOR_BLACK);
    gfx->drawRoundRect(12, 40, 456, 32, 5, COLOR_CYAN);
    gfx->setTextSize(2);
    gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
    gfx->setCursor(20, 48);
    gfx->printf("%-15s", renameBuffer);
}

void drawRenameKeyboard()
{
    gfx->fillScreen(COLOR_BLACK);
    gfx->setTextSize(2);
    gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
    gfx->setCursor(12, 12);
    gfx->print("Rename battery");

    drawRenameField();

    const char *rows[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL-"};
    const int rowY[] = {78, 114, 150};
    char label[2] = {'\0', '\0'};
    for (uint8_t row = 0; row < 3; ++row) {
        for (uint8_t key = 0; key < 10; ++key) {
            label[0] = rows[row][key];
            drawKeyboardKey(10 + key * 46, rowY[row], 44, label);
        }
    }

    const char *lastRow = "ZXCVBNM";
    for (uint8_t key = 0; key < 7; ++key) {
        label[0] = lastRow[key];
        drawKeyboardKey(44 + key * 44, 186, 40, label);
    }
    drawKeyboardKey(356, 186, 80, "BACK");
    drawKeyboardKey(10, 228, 150, "SPACE");
    drawKeyboardKey(170, 228, 135, "CANCEL", COLOR_AMBER);
    drawKeyboardKey(315, 228, 155, "SAVE", COLOR_GREEN);
}

void openRenameKeyboard()
{
    const SavedBm6Device *device = registry.active();
    if (device == nullptr) {
        drawSettingsStatus("No saved battery to rename");
        return;
    }
    stopSettingsScan();
    settingsScanPending = false;
    std::strncpy(renameBuffer, device->name, sizeof(renameBuffer) - 1);
    renameBuffer[sizeof(renameBuffer) - 1] = '\0';
    renameLength = static_cast<uint8_t>(std::strlen(renameBuffer));
    renameActive = true;
    drawRenameKeyboard();
}

void copySortedScanDevices(BleScanDevice *devices, uint8_t &deviceCount)
{
    BleScanDevice allDevices[MAX_SCAN_DEVICES];
    uint8_t allDeviceCount = 0;
    portENTER_CRITICAL(&scanDevicesMux);
    allDeviceCount = scanDeviceCount;
    std::memcpy(allDevices, scanDevices, sizeof(allDevices));
    portEXIT_CRITICAL(&scanDevicesMux);

    deviceCount = 0;
    for (uint8_t i = 0; i < allDeviceCount; ++i) {
        if (allDevices[i].lastSeenScan == settingsScanGeneration) {
            devices[deviceCount++] = allDevices[i];
        }
    }

    std::sort(devices, devices + deviceCount, [](const BleScanDevice &a, const BleScanDevice &b) {
        const bool aSaved = savedDeviceIndex(a.address) >= 0;
        const bool bSaved = savedDeviceIndex(b.address) >= 0;
        if (aSaved != bSaved) {
            return aSaved;
        }
        if (a.bm6Service != b.bm6Service) {
            return a.bm6Service;
        }
        if (isNewThisScan(a) != isNewThisScan(b)) {
            return isNewThisScan(a);
        }
        if (shouldSkipDeviceTest(a) != shouldSkipDeviceTest(b)) {
            return !shouldSkipDeviceTest(a);
        }
        return a.rssi > b.rssi;
    });
}

uint8_t settingsPageCount(uint8_t deviceCount)
{
    return std::max<uint8_t>(1, (deviceCount + SETTINGS_ROWS_PER_PAGE - 1) / SETTINGS_ROWS_PER_PAGE);
}

void drawPageControls(uint8_t deviceCount)
{
    const uint8_t pageCount = settingsPageCount(deviceCount);
    if (settingsPage >= pageCount) {
        settingsPage = pageCount - 1;
    }

    clearTextArea(12, 236, 456, 28);
    gfx->setTextSize(1);
    gfx->drawRoundRect(16, 238, 78, 24, 5, settingsPage > 0 ? COLOR_MUTED : COLOR_PANEL);
    gfx->setTextColor(settingsPage > 0 ? COLOR_WHITE : COLOR_MUTED, COLOR_BLACK);
    gfx->setCursor(36, 246);
    gfx->print("PREV");

    gfx->setTextColor(COLOR_MUTED, COLOR_BLACK);
    gfx->setCursor(198, 246);
    gfx->printf("Page %u/%u", static_cast<unsigned>(settingsPage + 1), static_cast<unsigned>(pageCount));

    gfx->drawRoundRect(386, 238, 78, 24, 5, settingsPage + 1 < pageCount ? COLOR_MUTED : COLOR_PANEL);
    gfx->setTextColor(settingsPage + 1 < pageCount ? COLOR_WHITE : COLOR_MUTED, COLOR_BLACK);
    gfx->setCursor(410, 246);
    gfx->print("NEXT");
}

void drawScanResults()
{
    BleScanDevice devices[MAX_SCAN_DEVICES];
    uint8_t deviceCount = 0;
    copySortedScanDevices(devices, deviceCount);

    clearTextArea(12, 98, 456, 134);
    gfx->setTextSize(1);
    if (deviceCount == 0) {
        gfx->setTextColor(COLOR_AMBER, COLOR_BLACK);
        gfx->setCursor(18, 112);
        gfx->print("No BLE devices found");
        drawPageControls(deviceCount);
        return;
    }

    const uint8_t start = settingsPage * SETTINGS_ROWS_PER_PAGE;
    const uint8_t end = std::min<uint8_t>(deviceCount, start + SETTINGS_ROWS_PER_PAGE);
    for (uint8_t i = start; i < end; ++i) {
        const uint8_t visibleRow = i - start;
        const int y = 102 + visibleRow * 18;
        clearTextArea(18, y, 438, 12);
        const bool skipped = shouldSkipDeviceTest(devices[i]);
        const int8_t savedIndex = savedDeviceIndex(devices[i].address);
        const bool active = savedIndex >= 0 && savedIndex == registry.activeIndex();
        gfx->setTextColor(active || devices[i].bm6Service ? COLOR_GREEN : (skipped ? COLOR_AMBER : COLOR_WHITE), COLOR_BLACK);
        gfx->setCursor(18, y);
        gfx->printf("%u %s", static_cast<unsigned>(visibleRow + 1), devices[i].address);
        gfx->setTextColor(COLOR_MUTED, COLOR_BLACK);
        gfx->setCursor(174, y);
        gfx->printf("%4d", devices[i].rssi);
        gfx->setCursor(220, y);
        const char *name = devices[i].name[0] ? devices[i].name : "(no name)";
        gfx->printf("%.18s", name);
        if (active) {
            gfx->setTextColor(COLOR_GREEN, COLOR_BLACK);
            gfx->setCursor(384, y);
            gfx->print("ACTIVE");
        } else if (savedIndex >= 0) {
            gfx->setTextColor(COLOR_CYAN, COLOR_BLACK);
            gfx->setCursor(390, y);
            gfx->print("SAVED");
        } else if (devices[i].bm6Service) {
            gfx->setTextColor(COLOR_GREEN, COLOR_BLACK);
            gfx->setCursor(390, y);
            gfx->print("BM6");
        } else if (isNewThisScan(devices[i])) {
            gfx->setTextColor(COLOR_CYAN, COLOR_BLACK);
            gfx->setCursor(390, y);
            gfx->print("NEW");
        } else if (skipped) {
            gfx->setTextColor(COLOR_AMBER, COLOR_BLACK);
            gfx->setCursor(390, y);
            gfx->print("SKIP");
        }
    }
    drawPageControls(deviceCount);
}

bool scanDeviceAtPoint(const TouchPoint &point, BleScanDevice &device)
{
    if (point.y < 102 || point.y >= 102 + SETTINGS_ROWS_PER_PAGE * 18) {
        return false;
    }

    const uint8_t visibleRow = static_cast<uint8_t>((point.y - 102) / 18);
    const uint8_t row = settingsPage * SETTINGS_ROWS_PER_PAGE + visibleRow;
    BleScanDevice devices[MAX_SCAN_DEVICES];
    uint8_t deviceCount = 0;
    copySortedScanDevices(devices, deviceCount);
    if (row >= deviceCount) {
        return false;
    }

    device = devices[row];
    return true;
}

bool scanDeviceAtRow(uint8_t row, BleScanDevice &device)
{
    row = settingsPage * SETTINGS_ROWS_PER_PAGE + row;
    BleScanDevice devices[MAX_SCAN_DEVICES];
    uint8_t deviceCount = 0;
    copySortedScanDevices(devices, deviceCount);
    if (row >= deviceCount) {
        return false;
    }

    device = devices[row];
    return true;
}

bool loadActiveSavedDevice()
{
    const SavedBm6Device *device = registry.active();
    if (device == nullptr) {
        bm6TargetSelected = false;
        bm6.setPreferredAddress("", 1);
        history.begin(0);
        haveReading = false;
        return false;
    }

    bm6.setPreferredAddress(device->address, device->addressType);
    history.begin(device->historySlot);
    haveReading = history.latest(latestReading);
    if (haveReading && latestReading.rssi == 0) {
        latestReading.rssi = device->lastRssi;
    }
    bm6TargetSelected = true;
    Serial.printf("Active BM6 %s %s type %u history %u\n", device->name, device->address,
                  device->addressType, device->historySlot);
    return true;
}

bool selectSavedDeviceRelative(int8_t direction)
{
    if (bm6PollActive) {
        setStatus("Battery update in progress");
        return false;
    }
    if (!registry.selectRelative(direction)) {
        return false;
    }
    loadActiveSavedDevice();
    consecutivePollFailures = 0;
    nextPollAtMs = millis();
    redrawDashboard();
    return true;
}

bool saveSelectedBm6(const BleScanDevice &device)
{
    const int8_t index = registry.addOrSelect(
        device.address, device.addressType, device.name, device.rssi
    );
    if (index < 0) {
        return false;
    }
    return loadActiveSavedDevice();
}

void testSelectedDevice(const BleScanDevice &device)
{
    if (bm6PollActive) {
        drawSettingsStatus("Waiting for battery update...");
        return;
    }
    stopSettingsScan();
    drawSettingsStatus("Testing selected device as BM6...");
    delay(300);

    if (shouldSkipDeviceTest(device)) {
        drawSettingsStatus("Skipped named medical device");
        Serial.printf("Skipped BLE test for %s name=\"%s\"\n", device.address, device.name);
        return;
    }

    BatteryReading reading;
    Bm6PollResult result = bm6.pollAddress(device.address, device.addressType, device.rssi, reading);
    Bm6PollResult firstResult = result;
    uint8_t addressType = device.addressType;
    if (result == Bm6PollResult::ConnectFailed || result == Bm6PollResult::ServiceMissing) {
        const uint8_t alternateType = device.addressType == 0 ? 1 : 0;
        Serial.printf("BM6 test retry for %s with address type %u\n", device.address, alternateType);
        result = bm6.pollAddress(device.address, alternateType, device.rssi, reading);
        if (result == Bm6PollResult::Ok) {
            addressType = alternateType;
        } else if (firstResult != Bm6PollResult::ConnectFailed && result == Bm6PollResult::ConnectFailed) {
            result = firstResult;
        }
    }
    if (result != Bm6PollResult::Ok) {
        char buffer[64];
        if (result == Bm6PollResult::ConnectFailed && device.rssi < -85) {
            snprintf(buffer, sizeof(buffer), "Connect failed: move BM6 closer");
        } else {
            snprintf(buffer, sizeof(buffer), "Not BM6: %s", pollResultText(result));
        }
        drawSettingsStatus(buffer);
        Serial.printf("BM6 test failed for %s RSSI %d: %s\n",
                      device.address, device.rssi, pollResultText(result));
        return;
    }

    BleScanDevice selected = device;
    selected.addressType = addressType;
    if (!saveSelectedBm6(selected)) {
        drawSettingsStatus("Could not save BM6 (4 device limit)");
        return;
    }
    latestReading = reading;
    haveReading = true;
    history.addIfDue(reading);

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "Saved BM6 %s %.2fV", device.address, reading.voltage);
    drawSettingsStatus(buffer);
    Serial.printf("Saved BM6 %s type %u %.2fV %u%% %dC\n",
                  device.address, addressType, reading.voltage, reading.socPercent, reading.temperatureC);
}

void stopSettingsScan()
{
    if (!settingsScanActive) {
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

void openSettings()
{
    if (!bm6PollActive) {
        stopSettingsScan();
    }
    settingsScanPending = false;
    settingsShowingResults = false;
    renameActive = false;
    currentScreen = Screen::Settings;
    drawSettingsHeader();
    drawSettingsStatus("BM6 background updates active");
    drawSavedDeviceBar();
    drawSettingsHome();
    Serial.println("Settings opened");
}

void beginSettingsScan()
{
    currentScreen = Screen::Settings;
    renameActive = false;
    if (bm6PollActive) {
        settingsScanPending = true;
        drawSettingsStatus("Scan queued after battery update");
        return;
    }

    stopSettingsScan();
    settingsScanPending = false;
    settingsShowingResults = true;
    scanResultsDirty = false;
    settingsPage = 0;
    ++settingsScanGeneration;
    if (settingsScanGeneration == 0) {
        settingsScanGeneration = 1;
        portENTER_CRITICAL(&scanDevicesMux);
        scanDeviceCount = 0;
        std::memset(scanDevices, 0, sizeof(scanDevices));
        portEXIT_CRITICAL(&scanDevicesMux);
    }
    drawSettingsHeader();
    drawSettingsStatus("Scanning nearby BLE devices for 30s...");
    drawSavedDeviceBar();
    clearTextArea(12, 98, 456, 134);

    Serial.println("Settings BLE scan starting");

    bm6.begin();

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
        BleScanDevice devices[MAX_SCAN_DEVICES];
        uint8_t deviceCount = 0;
        copySortedScanDevices(devices, deviceCount);
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
        drawSettingsStatus("Tap row to test BM6, SCAN to rescan");
        drawScanResults();
        scanResultsDirty = false;
        BleScanDevice devices[MAX_SCAN_DEVICES];
        uint8_t deviceCount = 0;
        copySortedScanDevices(devices, deviceCount);
        Serial.printf("Settings BLE scan complete, %u devices\n", deviceCount);
    } else if (settingsScanActive && scanResultsDirty && now - lastScanDrawMs >= 250) {
        drawScanResults();
        scanResultsDirty = false;
        lastScanDrawMs = now;
    }
}

void bm6PollTask(void *)
{
    BatteryReading reading;
    Bm6PollResult result = bm6PollJob.useDirectAddress
        ? bm6.pollAddress(bm6PollJob.address, bm6PollJob.addressType, bm6PollJob.rssi, reading)
        : bm6.poll(reading);

    // A saved address normally reconnects fastest directly. If that fails, an
    // advertisement scan refreshes the address type and wakes less-cooperative units.
    if (bm6PollJob.useDirectAddress && result != Bm6PollResult::Ok) {
        Serial.printf("Direct BM6 poll failed (%s), trying scan fallback\n", pollResultText(result));
        result = bm6.poll(reading);
    }

    portENTER_CRITICAL(&bm6PollMux);
    bm6PendingReading = reading;
    bm6PendingResult = result;
    bm6PollResultReady = true;
    bm6PollActive = false;
    portEXIT_CRITICAL(&bm6PollMux);
    vTaskDelete(nullptr);
}

void startBm6Poll()
{
    if (bm6PollActive || settingsScanActive || settingsScanPending) {
        return;
    }
    if (!bm6TargetSelected && !hasCompileTimeBm6Address()) {
        setStatus("Select BM6 in settings");
        nextPollAtMs = millis() + BM6_RECONNECT_INTERVAL_MS;
        return;
    }

    bm6PollJob = {};
    const SavedBm6Device *savedDevice = registry.active();
    if (savedDevice != nullptr) {
        std::strncpy(bm6PollJob.address, savedDevice->address, sizeof(bm6PollJob.address) - 1);
        bm6PollJob.addressType = savedDevice->addressType;
        bm6PollJob.rssi = savedDevice->lastRssi;
        bm6PollJob.useDirectAddress = true;
    }

    bm6PollResultReady = false;
    bm6PollActive = true;
    if (!haveReading) {
        setStatus(savedDevice == nullptr ? "Scanning BM6" : "Connecting BM6");
    } else if (std::strcmp(currentStatus, "Starting") == 0) {
        setStatus("BM6 monitoring");
    }
    if (xTaskCreatePinnedToCore(bm6PollTask, "bm6-poll", 12288, nullptr, 1, nullptr, 0) != pdPASS) {
        bm6PollActive = false;
        setStatus(haveReading ? "BM6 retry pending" : "BM6 unavailable");
        nextPollAtMs = millis() + BM6_RECONNECT_INTERVAL_MS;
        Serial.println("Could not start BM6 poll task");
    }
}

void serviceBm6Poll()
{
    if (!bm6PollResultReady) {
        return;
    }

    BatteryReading reading;
    Bm6PollResult result;
    portENTER_CRITICAL(&bm6PollMux);
    reading = bm6PendingReading;
    result = bm6PendingResult;
    bm6PollResultReady = false;
    portEXIT_CRITICAL(&bm6PollMux);

    if (result == Bm6PollResult::Ok) {
        consecutivePollFailures = 0;
        latestReading = reading;
        haveReading = true;
        history.addIfDue(reading);
        registry.updateRssi(registry.activeIndex(), reading.rssi);
        setStatus("BM6 connected");
        if (currentScreen == Screen::Dash) {
            drawDashboardPage();
        }
        Serial.printf("BM6 %.2fV %u%% %dC RSSI %d\n",
                      reading.voltage, reading.socPercent, reading.temperatureC, reading.rssi);
        nextPollAtMs = millis() + BM6_POLL_INTERVAL_MS;
    } else {
        if (consecutivePollFailures < 255) {
            ++consecutivePollFailures;
        }
        if (!haveReading) {
            setStatus("BM6 unavailable - retrying");
        } else if (consecutivePollFailures >= 6) {
            setStatus("BM6 signal lost - retrying");
        }
        Serial.printf("BM6 update failed: %s\n", pollResultText(result));
        nextPollAtMs = millis() + BM6_RECONNECT_INTERVAL_MS;
    }
}

void appendRenameCharacter(char character)
{
    if (renameLength >= sizeof(renameBuffer) - 1) {
        return;
    }
    renameBuffer[renameLength++] = character;
    renameBuffer[renameLength] = '\0';
    drawRenameField();
}

void finishRename(bool saveName)
{
    if (saveName) {
        while (renameLength > 0 && renameBuffer[renameLength - 1] == ' ') {
            renameBuffer[--renameLength] = '\0';
        }
        if (renameLength == 0 || !registry.rename(registry.activeIndex(), renameBuffer)) {
            drawRenameField();
            return;
        }
    }

    renameActive = false;
    openSettings();
    if (saveName) {
        drawSettingsStatus("Battery name saved");
        Serial.printf("Battery renamed to %s\n", renameBuffer);
    }
}

void handleRenameTap(const TouchPoint &point)
{
    const char *rows[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL-"};
    const int rowY[] = {78, 114, 150};
    for (uint8_t row = 0; row < 3; ++row) {
        if (point.y < rowY[row] || point.y > rowY[row] + 30 || point.x < 10) {
            continue;
        }
        const int offset = point.x - 10;
        const uint8_t key = static_cast<uint8_t>(offset / 46);
        if (key < 10 && offset % 46 < 44) {
            appendRenameCharacter(rows[row][key]);
            return;
        }
    }

    if (point.y >= 186 && point.y <= 216) {
        if (point.x >= 44 && point.x < 352) {
            const int offset = point.x - 44;
            const uint8_t key = static_cast<uint8_t>(offset / 44);
            if (key < 7 && offset % 44 < 40) {
                appendRenameCharacter("ZXCVBNM"[key]);
            }
            return;
        }
        if (point.x >= 356 && point.x <= 436 && renameLength > 0) {
            renameBuffer[--renameLength] = '\0';
            drawRenameField();
        }
        return;
    }

    if (point.y >= 228 && point.y <= 262) {
        if (point.x >= 10 && point.x <= 160) {
            if (renameLength > 0 && renameBuffer[renameLength - 1] != ' ') {
                appendRenameCharacter(' ');
            }
        } else if (point.x >= 170 && point.x <= 305) {
            finishRename(false);
        } else if (point.x >= 315 && point.x <= 470) {
            finishRename(true);
        }
    }
}

void handleTouchTap(const TouchPoint &point)
{
    Serial.printf("Touch %d,%d\n", point.x, point.y);
    if (currentScreen == Screen::Settings && renameActive) {
        handleRenameTap(point);
        return;
    }
    if (currentScreen == Screen::Dash && point.x >= 438 && point.y <= 40) {
        openSettings();
        return;
    }
    if (currentScreen == Screen::Dash && point.y <= 42 && point.x <= 34) {
        selectSavedDeviceRelative(-1);
        return;
    }
    if (currentScreen == Screen::Dash && point.y <= 42 && point.x >= 194 && point.x <= 232) {
        selectSavedDeviceRelative(1);
        return;
    }
    if (currentScreen == Screen::Dash && point.y >= 238) {
        dashPage = point.x < 160
            ? DashPage::Overview
            : (point.x < 320 ? DashPage::History : DashPage::Tests);
        drawDashboardPage();
        return;
    }
    if (currentScreen == Screen::Dash && dashPage == DashPage::History &&
        point.y >= 48 && point.y <= 78) {
        if (point.x >= 14 && point.x < 76) {
            historyRange = HistoryRange::SixHours;
        } else if (point.x >= 84 && point.x < 146) {
            historyRange = HistoryRange::OneDay;
        } else if (point.x >= 154 && point.x < 216) {
            historyRange = HistoryRange::ThreeDays;
        }
        drawHistoryPage();
        return;
    }
    if (currentScreen == Screen::Settings && point.x >= 8 && point.x <= 42 && point.y >= 6 && point.y <= 36) {
        stopSettingsScan();
        Serial.println("Settings closed");
        redrawDashboard();
        return;
    }
    if (currentScreen == Screen::Settings && point.x >= 362 && point.x <= 466 && point.y >= 6 && point.y <= 36) {
        beginSettingsScan();
        return;
    }
    if (currentScreen == Screen::Settings && point.y >= 64 && point.y <= 96 && registry.count() > 0) {
        if (point.x >= 386 && point.x <= 464) {
            openRenameKeyboard();
            return;
        }
        if (bm6PollActive) {
            drawSettingsStatus("Waiting for battery update...");
            return;
        }
        const int width = 362 / registry.count();
        if (point.x >= 16 && point.x < 378) {
            const uint8_t index = static_cast<uint8_t>((point.x - 16) / width);
            if (index < registry.count() && registry.select(index)) {
                loadActiveSavedDevice();
                consecutivePollFailures = 0;
                nextPollAtMs = millis();
                drawSavedDeviceBar();
                if (settingsShowingResults) {
                    drawScanResults();
                } else {
                    drawSettingsHome();
                }
                drawSettingsStatus("Saved battery selected");
            }
        }
        return;
    }
    if (currentScreen == Screen::Settings && point.y >= 238 && point.y <= 262) {
        BleScanDevice devices[MAX_SCAN_DEVICES];
        uint8_t deviceCount = 0;
        copySortedScanDevices(devices, deviceCount);
        const uint8_t pageCount = settingsPageCount(deviceCount);
        if (point.x >= 16 && point.x <= 94 && settingsPage > 0) {
            --settingsPage;
            drawScanResults();
        } else if (point.x >= 386 && point.x <= 464 && settingsPage + 1 < pageCount) {
            ++settingsPage;
            drawScanResults();
        }
        return;
    }

    BleScanDevice selected;
    if (currentScreen == Screen::Settings && scanDeviceAtPoint(point, selected)) {
        testSelectedDevice(selected);
    }
}

void handleTouch()
{
    TouchPoint point;
    bool contact = false;
    const bool updated = touch.read(point, contact);
    if (!updated) {
        if (touchWasDown && millis() - lastTouchContactAtMs > 250) {
            touchWasDown = false;
        }
        return;
    }
    if (contact && !touchWasDown) {
        touchWasDown = true;
        lastTouchContactAtMs = millis();
        handleTouchTap(point);
    } else if (contact) {
        lastTouchContactAtMs = millis();
    } else if (!contact) {
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
        openSettings();
    } else if (command == 'd') {
        stopSettingsScan();
        Serial.println("Settings closed");
        redrawDashboard();
    } else if (command == '[' && currentScreen == Screen::Dash) {
        selectSavedDeviceRelative(-1);
    } else if (command == ']' && currentScreen == Screen::Dash) {
        selectSavedDeviceRelative(1);
    } else if (command == 'r' && currentScreen == Screen::Settings) {
        beginSettingsScan();
    } else if (command >= '1' && command <= '9' && currentScreen == Screen::Settings) {
        BleScanDevice selected;
        if (scanDeviceAtRow(static_cast<uint8_t>(command - '1'), selected)) {
            testSelectedDevice(selected);
        }
    } else if ((command == 'n' || command == 'p') && currentScreen == Screen::Settings) {
        BleScanDevice devices[MAX_SCAN_DEVICES];
        uint8_t deviceCount = 0;
        copySortedScanDevices(devices, deviceCount);
        const uint8_t pageCount = settingsPageCount(deviceCount);
        if (command == 'n' && settingsPage + 1 < pageCount) {
            ++settingsPage;
            drawScanResults();
        } else if (command == 'p' && settingsPage > 0) {
            --settingsPage;
            drawScanResults();
        }
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
    gfx->invertDisplay(false);
    gfx->setRotation(DISPLAY_ROTATION);
    touch.begin();

    registry.begin();
    loadActiveSavedDevice();
    if (hasCompileTimeBm6Address()) {
        bm6.setPreferredAddress(BM6_MAC_ADDRESS, 1);
        bm6TargetSelected = true;
    }

    redrawDashboard();

    bm6.begin();
    if (bm6TargetSelected) {
        startBm6Poll();
    } else {
        setStatus("Select BM6 in settings");
        nextPollAtMs = millis() + BM6_RECONNECT_INTERVAL_MS;
    }
}

void loop()
{
    const uint32_t now = millis();
    handleSerial();
    handleTouch();
    serviceBm6Poll();
    if (settingsScanPending && !bm6PollActive && currentScreen == Screen::Settings) {
        beginSettingsScan();
    }
    serviceSettingsScan();

    if (!bm6PollActive && !settingsScanActive && !settingsScanPending &&
        static_cast<int32_t>(now - nextPollAtMs) >= 0) {
        startBm6Poll();
    }
    if (currentScreen == Screen::Dash && now - lastUiTickMs >= 1000) {
        drawStatus();
        lastUiTickMs = now;
    }
    if (currentScreen == Screen::Dash && now - lastChartDrawMs >= 60000) {
        if (dashPage == DashPage::Overview) {
            drawOverview();
        } else if (dashPage == DashPage::History) {
            drawHistoryPage();
        }
        drawNavigation();
        lastChartDrawMs = now;
    }
    delay(50);
}
