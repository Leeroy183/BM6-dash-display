#pragma once

#include <Arduino.h>

// Set this to your BM6 MAC address when known. Leave the default to scan by
// BLE name or BM6 service UUID, which is slower but useful for first setup.
static constexpr char BM6_MAC_ADDRESS[] = "00:00:00:00:00:00";
static constexpr char BM6_ADVERTISED_NAME[] = "BM6";

static constexpr uint32_t BM6_SCAN_TIMEOUT_MS = 10000;
static constexpr uint32_t BM6_PACKET_TIMEOUT_MS = 5000;
static constexpr uint32_t BM6_POLL_INTERVAL_MS = 60000;
static constexpr uint32_t HISTORY_SAMPLE_INTERVAL_MS = 5UL * 60UL * 1000UL;

static constexpr float BATTERY_WARNING_VOLTS = 12.20f;
static constexpr float BATTERY_CRITICAL_VOLTS = 12.00f;

static constexpr size_t HISTORY_CAPACITY = 3UL * 24UL * 60UL / 5UL;
static constexpr uint16_t HISTORY_CHART_POINTS = 240;
