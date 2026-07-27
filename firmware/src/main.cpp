#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "BatteryStore.h"
#include "Bm6Client.h"
#include "DashUi.h"
#include "config.h"
#include "lvgl_v8_port.h"

using namespace esp_panel::board;
using namespace esp_panel::drivers;

namespace {
Bm6Client bm6;
BatteryStore history;
DashUi ui;
uint32_t nextPollAtMs = 0;
uint32_t lastCountdownDrawMs = 0;

const char *pollResultText(Bm6PollResult result)
{
    switch (result) {
        case Bm6PollResult::Ok:
            return "Connected";
        case Bm6PollResult::NotFound:
            return "BM6 not found";
        case Bm6PollResult::ConnectFailed:
            return "BLE connect failed";
        case Bm6PollResult::ServiceMissing:
            return "BM6 service missing";
        case Bm6PollResult::CharacteristicMissing:
            return "BM6 characteristic missing";
        case Bm6PollResult::SubscribeFailed:
            return "Notify subscribe failed";
        case Bm6PollResult::WriteFailed:
            return "Read command failed";
        case Bm6PollResult::Timeout:
            return "BM6 response timeout";
        case Bm6PollResult::InvalidPacket:
            return "Invalid BM6 packet";
    }
    return "Unknown status";
}

void initDisplay()
{
    Board *board = new Board();
    board->init();
#if LVGL_PORT_AVOID_TEARING_MODE
    auto lcd = board->getLCD();
    lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
    auto lcdBus = lcd->getBus();
    if (lcdBus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        static_cast<BusRGB *>(lcdBus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
    }
#endif
#endif
    assert(board->begin());
    assert(lvgl_port_init(board->getLCD(), board->getTouch()));
}

void pollBm6Now()
{
    ui.setStatus("Scanning BM6");
    BatteryReading reading;
    const Bm6PollResult result = bm6.poll(reading);
    ui.setStatus(pollResultText(result));

    if (result == Bm6PollResult::Ok) {
        history.add(reading);
        ui.setReading(reading, history);
    }

    nextPollAtMs = millis() + BM6_POLL_INTERVAL_MS;
}
} // namespace

void setup()
{
    Serial.begin(115200);
    delay(250);
    Serial.println("BM6 standalone dash starting");

    initDisplay();
    ui.begin();
    bm6.begin();

    pollBm6Now();
}

void loop()
{
    const uint32_t now = millis();
    if (static_cast<int32_t>(now - nextPollAtMs) >= 0) {
        pollBm6Now();
    }

    if (now - lastCountdownDrawMs > 1000) {
        const uint32_t remaining = static_cast<int32_t>(nextPollAtMs - now) > 0 ? nextPollAtMs - now : 0;
        ui.setNextPollCountdown(remaining);
        lastCountdownDrawMs = now;
    }

    delay(50);
}
