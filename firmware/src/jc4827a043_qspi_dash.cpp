#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <algorithm>
#include <cmath>
#include "Bm6Client.h"
#include "PersistentHistory.h"
#include "config.h"

namespace {
constexpr int GFX_BL = 1;
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

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    45 /* CS */, 47 /* SCK */, 21 /* D0 */, 48 /* D1 */, 40 /* D2 */, 39 /* D3 */
);
Arduino_GFX *gfx = new Arduino_NV3041A(bus, GFX_NOT_DEFINED /* RST */, 0 /* rotation */, true /* IPS */);

Bm6Client bm6;
PersistentHistory history;
BatteryReading latestReading;
bool haveReading = false;
uint32_t nextPollAtMs = 0;
uint32_t lastUiTickMs = 0;
uint32_t lastChartDrawMs = 0;
char currentStatus[36] = "Starting";

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

void drawStaticLayout()
{
    gfx->fillScreen(COLOR_BLACK);
    gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
    gfx->setTextSize(2);
    gfx->setCursor(14, 12);
    gfx->print("BM6 Dash Display");

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
    clearTextArea(250, 10, 216, 18);
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
    drawStatus();
    Serial.println(currentStatus);
}

void pollBm6Now()
{
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
    if (static_cast<int32_t>(now - nextPollAtMs) >= 0) {
        pollBm6Now();
    }
    if (now - lastUiTickMs >= 1000) {
        drawFooter();
        lastUiTickMs = now;
    }
    if (now - lastChartDrawMs >= 60000) {
        drawHistoryChart();
        lastChartDrawMs = now;
    }
    delay(50);
}
