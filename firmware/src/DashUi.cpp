#include "DashUi.h"

#include <algorithm>
#include <cmath>
#include "config.h"
#include "lvgl_v8_port.h"

namespace {
const lv_color_t COLOR_BACKGROUND = LV_COLOR_MAKE(16, 18, 22);
const lv_color_t COLOR_PANEL = LV_COLOR_MAKE(33, 37, 43);
const lv_color_t COLOR_PANEL_ALT = LV_COLOR_MAKE(41, 45, 51);
const lv_color_t COLOR_TEXT = LV_COLOR_MAKE(239, 244, 248);
const lv_color_t COLOR_MUTED = LV_COLOR_MAKE(148, 158, 168);
const lv_color_t COLOR_GREEN = LV_COLOR_MAKE(52, 199, 89);
const lv_color_t COLOR_AMBER = LV_COLOR_MAKE(255, 184, 77);
const lv_color_t COLOR_RED = LV_COLOR_MAKE(255, 82, 82);
const lv_color_t COLOR_BLUE = LV_COLOR_MAKE(64, 156, 255);

lv_obj_t *createPanel(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, COLOR_PANEL, 0);
    lv_obj_set_style_border_color(panel, LV_COLOR_MAKE(64, 70, 78), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 18, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

lv_obj_t *createCaption(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, COLOR_MUTED, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
    return label;
}

const char *ageText(uint32_t sampledAtMs)
{
    static char buffer[24];
    const uint32_t ageSeconds = (millis() - sampledAtMs) / 1000;
    if (ageSeconds < 5) {
        return "updated now";
    }
    if (ageSeconds < 60) {
        snprintf(buffer, sizeof(buffer), "%lu sec ago", static_cast<unsigned long>(ageSeconds));
        return buffer;
    }
    snprintf(buffer, sizeof(buffer), "%lu min ago", static_cast<unsigned long>(ageSeconds / 60));
    return buffer;
}
} // namespace

void DashUi::begin()
{
    if (!lvgl_port_lock(-1)) {
        return;
    }

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, COLOR_BACKGROUND, 0);
    lv_obj_set_style_text_color(screen, COLOR_TEXT, 0);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "BM6 Battery Dash");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT, 0);
    lv_obj_set_pos(title, 24, 18);

    statusLabel_ = lv_label_create(screen);
    lv_label_set_text(statusLabel_, "Starting");
    lv_obj_set_style_text_font(statusLabel_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(statusLabel_, COLOR_MUTED, 0);
    lv_obj_align(statusLabel_, LV_ALIGN_TOP_RIGHT, -24, 24);

    lv_obj_t *voltagePanel = createPanel(screen, 24, 68, 360, 180);
    createCaption(voltagePanel, "VOLTAGE");
    voltageLabel_ = lv_label_create(voltagePanel);
    lv_label_set_text(voltageLabel_, "--.-- V");
    lv_obj_set_style_text_font(voltageLabel_, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(voltageLabel_, COLOR_MUTED, 0);
    lv_obj_align(voltageLabel_, LV_ALIGN_LEFT_MID, 0, 10);
    voltageSubLabel_ = lv_label_create(voltagePanel);
    lv_label_set_text(voltageSubLabel_, "waiting for first sample");
    lv_obj_set_style_text_font(voltageSubLabel_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(voltageSubLabel_, COLOR_MUTED, 0);
    lv_obj_align(voltageSubLabel_, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *socPanel = createPanel(screen, 408, 68, 170, 180);
    createCaption(socPanel, "STATE");
    socLabel_ = lv_label_create(socPanel);
    lv_label_set_text(socLabel_, "--%");
    lv_obj_set_style_text_font(socLabel_, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(socLabel_, COLOR_BLUE, 0);
    lv_obj_align(socLabel_, LV_ALIGN_CENTER, 0, 10);

    lv_obj_t *temperaturePanel = createPanel(screen, 604, 68, 172, 180);
    createCaption(temperaturePanel, "TEMP");
    temperatureLabel_ = lv_label_create(temperaturePanel);
    lv_label_set_text(temperatureLabel_, "-- C");
    lv_obj_set_style_text_font(temperatureLabel_, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(temperatureLabel_, COLOR_TEXT, 0);
    lv_obj_align(temperatureLabel_, LV_ALIGN_CENTER, 0, 10);

    chart_ = lv_chart_create(screen);
    lv_obj_set_pos(chart_, 24, 280);
    lv_obj_set_size(chart_, 752, 150);
    lv_obj_set_style_bg_color(chart_, COLOR_PANEL_ALT, 0);
    lv_obj_set_style_border_color(chart_, LV_COLOR_MAKE(64, 70, 78), 0);
    lv_obj_set_style_border_width(chart_, 1, 0);
    lv_obj_set_style_radius(chart_, 8, 0);
    lv_chart_set_type(chart_, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(chart_, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_point_count(chart_, HISTORY_CHART_POINTS);
    lv_chart_set_div_line_count(chart_, 4, 8);
    lv_chart_set_range(chart_, LV_CHART_AXIS_PRIMARY_Y, 1150, 1300);
    voltageSeries_ = lv_chart_add_series(chart_, COLOR_GREEN, LV_CHART_AXIS_PRIMARY_Y);
    for (uint16_t i = 0; i < HISTORY_CHART_POINTS; ++i) {
        lv_chart_set_value_by_id(chart_, voltageSeries_, i, LV_CHART_POINT_NONE);
    }

    rangeLabel_ = lv_label_create(screen);
    lv_label_set_text(rangeLabel_, "Voltage history");
    lv_obj_set_style_text_font(rangeLabel_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(rangeLabel_, COLOR_MUTED, 0);
    lv_obj_set_pos(rangeLabel_, 28, 442);

    pollLabel_ = lv_label_create(screen);
    lv_label_set_text(pollLabel_, "Next poll --");
    lv_obj_set_style_text_font(pollLabel_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(pollLabel_, COLOR_MUTED, 0);
    lv_obj_align(pollLabel_, LV_ALIGN_BOTTOM_RIGHT, -24, -18);

    lvgl_port_unlock();
}

void DashUi::setStatus(const char *status)
{
    if (!statusLabel_ || !lvgl_port_lock(100)) {
        return;
    }
    lv_label_set_text(statusLabel_, status);
    lvgl_port_unlock();
}

void DashUi::setReading(const BatteryReading &reading, const BatteryStore &store)
{
    if (!voltageLabel_ || !lvgl_port_lock(250)) {
        return;
    }

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.2f V", reading.voltage);
    lv_label_set_text(voltageLabel_, buffer);
    setVoltageColor(reading.voltage);

    snprintf(buffer, sizeof(buffer), "%s | RSSI %d dBm", ageText(reading.sampledAtMs), reading.rssi);
    lv_label_set_text(voltageSubLabel_, buffer);

    snprintf(buffer, sizeof(buffer), "%u%%", static_cast<unsigned>(reading.socPercent));
    lv_label_set_text(socLabel_, buffer);
    lv_obj_set_style_text_color(socLabel_, reading.socPercent < 40 ? COLOR_AMBER : COLOR_BLUE, 0);

    snprintf(buffer, sizeof(buffer), "%d C", reading.temperatureC);
    lv_label_set_text(temperatureLabel_, buffer);

    updateChart(store);
    lvgl_port_unlock();
}

void DashUi::setNextPollCountdown(uint32_t milliseconds)
{
    if (!pollLabel_ || !lvgl_port_lock(50)) {
        return;
    }

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "Next poll %lus", static_cast<unsigned long>((milliseconds + 999) / 1000));
    lv_label_set_text(pollLabel_, buffer);
    lvgl_port_unlock();
}

void DashUi::updateChart(const BatteryStore &store)
{
    if (!chart_ || !voltageSeries_) {
        return;
    }

    if (store.size() > 0) {
        const int minRange = std::max(1050, static_cast<int>(std::floor((store.minVoltage() - 0.15f) * 100.0f)));
        const int maxRange = std::min(1500, static_cast<int>(std::ceil((store.maxVoltage() + 0.15f) * 100.0f)));
        lv_chart_set_range(chart_, LV_CHART_AXIS_PRIMARY_Y, minRange, std::max(maxRange, minRange + 60));

        char buffer[64];
        snprintf(buffer, sizeof(buffer), "Voltage history %.2f-%.2f V | %u samples",
                 store.minVoltage(), store.maxVoltage(), static_cast<unsigned>(store.size()));
        lv_label_set_text(rangeLabel_, buffer);
    }

    for (uint16_t i = 0; i < HISTORY_CHART_POINTS; ++i) {
        const int value = store.voltageHundredthsForChart(i, HISTORY_CHART_POINTS);
        lv_chart_set_value_by_id(
            chart_, voltageSeries_, i, value == BATTERY_STORE_NO_CHART_SAMPLE ? LV_CHART_POINT_NONE : value
        );
    }
    lv_chart_refresh(chart_);
}

void DashUi::setVoltageColor(float voltage)
{
    lv_color_t color = COLOR_GREEN;
    if (voltage < BATTERY_CRITICAL_VOLTS) {
        color = COLOR_RED;
    } else if (voltage < BATTERY_WARNING_VOLTS) {
        color = COLOR_AMBER;
    }
    lv_obj_set_style_text_color(voltageLabel_, color, 0);
}
