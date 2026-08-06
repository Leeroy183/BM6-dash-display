#include <Arduino.h>
#include <Arduino_GFX_Library.h>

namespace {
constexpr int GFX_BL = 2;
constexpr int SCREEN_WIDTH = 480;
constexpr int SCREEN_HEIGHT = 272;
constexpr uint16_t COLOR_BLACK = 0x0000;
constexpr uint16_t COLOR_WHITE = 0xffff;

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    40 /* DE */, 41 /* VSYNC */, 39 /* HSYNC */, 42 /* PCLK */,
    45 /* R0 */, 48 /* R1 */, 47 /* R2 */, 21 /* R3 */, 14 /* R4 */,
    5 /* G0 */, 6 /* G1 */, 7 /* G2 */, 15 /* G3 */, 16 /* G4 */, 4 /* G5 */,
    8 /* B0 */, 3 /* B1 */, 46 /* B2 */, 9 /* B3 */, 1 /* B4 */,
    0 /* hsync_polarity */, 1 /* hsync_front_porch */, 1 /* hsync_pulse_width */, 43 /* hsync_back_porch */,
    0 /* vsync_polarity */, 3 /* vsync_front_porch */, 1 /* vsync_pulse_width */, 12 /* vsync_back_porch */,
    1 /* pclk_active_neg */, 10000000 /* prefer_speed */, false /* useBigEndian */,
    0 /* de_idle_high */, 0 /* pclk_idle_high */, 0 /* bounce_buffer_size_px */
);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    SCREEN_WIDTH /* width */, SCREEN_HEIGHT /* height */, rgbpanel, 0 /* rotation */, true /* auto_flush */
);

uint16_t barColors[] = {
    0xf800, 0xfd20, 0xffe0, 0x07e0, 0x07ff, 0x001f, 0x8010, COLOR_WHITE
};

void drawSmokeScreen(uint32_t counter)
{
    const int barWidth = SCREEN_WIDTH / static_cast<int>(sizeof(barColors) / sizeof(barColors[0]));
    for (size_t i = 0; i < sizeof(barColors) / sizeof(barColors[0]); ++i) {
        gfx->fillRect(static_cast<int>(i) * barWidth, 0, barWidth + 1, SCREEN_HEIGHT, barColors[i]);
    }

    gfx->fillRoundRect(28, 56, 424, 160, 12, COLOR_BLACK);
    gfx->drawRoundRect(28, 56, 424, 160, 12, COLOR_WHITE);
    gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
    gfx->setTextSize(3);
    gfx->setCursor(52, 82);
    gfx->print("BM6 Dash Display");
    gfx->setTextSize(2);
    gfx->setCursor(52, 130);
    gfx->print("JC4827W543 smoke test");
    gfx->setCursor(52, 164);
    gfx->print("COM5 frame ");
    gfx->print(counter);
}
} // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("JC4827W543 smoke test starting");

    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);

    if (!gfx->begin()) {
        Serial.println("Display begin failed");
        return;
    }

    gfx->fillScreen(COLOR_BLACK);
    drawSmokeScreen(0);
    Serial.println("Display smoke screen drawn");
}

void loop()
{
    static uint32_t counter = 1;
    static uint32_t lastDrawMs = 0;

    if (millis() - lastDrawMs >= 1000) {
        drawSmokeScreen(counter++);
        Serial.println("Smoke test heartbeat");
        lastDrawMs = millis();
    }
}
