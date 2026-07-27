#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "BatteryStore.h"
#include "BatteryTypes.h"

class DashUi {
  public:
    void begin();
    void setStatus(const char *status);
    void setReading(const BatteryReading &reading, const BatteryStore &store);
    void setNextPollCountdown(uint32_t milliseconds);

  private:
    lv_obj_t *voltageLabel_ = nullptr;
    lv_obj_t *voltageSubLabel_ = nullptr;
    lv_obj_t *socLabel_ = nullptr;
    lv_obj_t *temperatureLabel_ = nullptr;
    lv_obj_t *statusLabel_ = nullptr;
    lv_obj_t *pollLabel_ = nullptr;
    lv_obj_t *rangeLabel_ = nullptr;
    lv_obj_t *chart_ = nullptr;
    lv_chart_series_t *voltageSeries_ = nullptr;

    void updateChart(const BatteryStore &store);
    void setVoltageColor(float voltage);
};
