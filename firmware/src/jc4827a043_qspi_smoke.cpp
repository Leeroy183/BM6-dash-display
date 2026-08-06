#include <Arduino.h>
#include <Arduino_GFX_Library.h>

namespace {
constexpr int GFX_BL = 1;
constexpr int SCREEN_WIDTH = 480;
constexpr int SCREEN_HEIGHT = 272;
constexpr uint16_t COLOR_BLACK = 0x0000;
constexpr uint16_t COLOR_WHITE = 0xffff;

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    45 /* CS */, 47 /* SCK */, 21 /* D0 */, 48 /* D1 */, 40 /* D2 */, 39 /* D3 */
);
Arduino_GFX *gfx = new Arduino_NV3041A(bus, GFX_NOT_DEFINED /* RST */, 0 /* rotation */, true /* IPS */);

uint16_t barColors[] = {
    0xf800, 0xfd20, 0xffe0, 0x07e0, 0x07ff, 0x001f, 0x8010, COLOR_WHITE
};

void drawStaticScreen()
{
    const int barWidth = SCREEN_WIDTH / static_cast<int>(sizeof(barColors) / sizeof(barColors[0]));
    for (size_t i = 0; i < sizeof(barColors) / sizeof(barColors[0]); ++i) {
        gfx->fillRect(static_cast<int>(i) * barWidth, 0, barWidth + 1, SCREEN_HEIGHT, barColors[i]);
    }

    gfx->fillRoundRect(26, 54, 428, 164, 12, COLOR_BLACK);
    gfx->drawRoundRect(26, 54, 428, 164, 12, COLOR_WHITE);
    gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
    gfx->setTextSize(3);
    gfx->setCursor(50, 82);
    gfx->print("BM6 Dash Display");
    gfx->setTextSize(2);
    gfx->setCursor(50, 130);
    gfx->print("QSPI NV3041A test");
}

void drawCounter(uint32_t counter)
{
    gfx->fillRect(50, 158, 260, 24, COLOR_BLACK);
    gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
    gfx->setTextSize(2);
    gfx->setCursor(50, 164);
    gfx->print("COM5 frame ");
    gfx->print(counter);
}
} // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("JC4827A043 QSPI smoke test starting");

    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);

    if (!gfx->begin()) {
        Serial.println("Display begin failed");
        return;
    }

    gfx->invertDisplay(true);
    gfx->setRotation(2);
    gfx->fillScreen(COLOR_BLACK);
    drawStaticScreen();
    drawCounter(0);
    Serial.println("QSPI display smoke screen drawn");
}

void loop()
{
    static uint32_t counter = 1;
    static uint32_t lastDrawMs = 0;

    if (millis() - lastDrawMs >= 1000) {
        drawCounter(counter++);
        Serial.println("QSPI smoke test heartbeat");
        lastDrawMs = millis();
    }
}
