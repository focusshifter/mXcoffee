#pragma once

#include <M5GFX.h>  // for lgfx::rgb888_t and helpers

#include <stdint.h>
#include <cstddef>
#include <string>
#include <vector>

#define SMOOTH_FONT

// 24-bit theme colours expressed as uint32_t via lgfx::color888 helper
using Color = uint32_t;


// Color theme
constexpr Color THEME_LIGHTBG      = lgfx::color888(0x9C, 0xCE, 0xC5);
constexpr Color THEME_LIGHTTEXT    = lgfx::color888(0xE6, 0xFF, 0xFF);

constexpr Color THEME_DARKBG       = lgfx::color888(0x03, 0x16, 0x1E);
constexpr Color THEME_DARKTEXT     = lgfx::color888(0x00, 0xC6, 0x31);

constexpr Color THEME_LIGHTACCENTBG   = lgfx::color888(0x96, 0xCB, 0xBB);
constexpr Color THEME_LIGHTACCENTTEXT = lgfx::color888(0x0A, 0x3B, 0x44);

// #define THEME_DARKACCENTBG 0x7AC8
// #define THEME_DARKACCENTTEXT 0x7AC8

constexpr Color THEME_DISABLEDBG      = lgfx::color888(0x7B, 0x10, 0x31);
constexpr Color THEME_DISABLEDTEXT    = lgfx::color888(0x31, 0x24, 0x00);

constexpr Color THEME_GRAPH_GOOD    = lgfx::color888(0xBD, 0xFF, 0xFF);
constexpr Color THEME_GRAPH_WARNING = lgfx::color888(0xD6, 0xA6, 0x7B);
constexpr Color THEME_GRAPH_BAD    = lgfx::color888(0xD6, 0xA6, 0x7B);

constexpr Color THEME_GRID_LINES   = lgfx::color888(0x00, 0x48, 0x52);

constexpr Color THEME_BAR_BG       = lgfx::color888(0x0A, 0x3B, 0x44);

// Panels

constexpr Color THEME_PANEL_OUTER_BG  = lgfx::color888(0x96, 0xCB, 0xBB);
constexpr Color THEME_PANEL_HEADER_TEXT = lgfx::color888(0x0A, 0x3B, 0x44);
constexpr Color THEME_PANEL_TEXT    = lgfx::color888(0xDD, 0xFE, 0xEE);
constexpr Color THEME_PANEL_INNER_BG = lgfx::color888(0x0A, 0x3B, 0x44);

#include "fonts/MoonGloss_16.h"

struct DeviceState {
  bool isAsleep;
  bool isBluetoothOn;
  bool deviceConnected;
  bool lastBTSendSuccessful;
  bool debugMode;
  unsigned long lastRefreshTime;
  unsigned long lastActivityTime;
  unsigned long lastWeightUpdateTime;  // For flow rate calculation
  unsigned long lastGraphChangeTime;
  int16_t lastPressure;
  int16_t lastGraphPressure;
  float lastGraphWeight;
  unsigned long timerStartTime;
  unsigned long shotStartTime;
  unsigned long shotTotalTime;
  bool isTimerRunning;
  void *pServer;

  float shotWeight;
  float lastShotWeight;
  float flowRate;
  unsigned long lastScaleSampleTime;
  unsigned long flowCalcStartTime;
  float flowCalcStartWeight;
  unsigned long screenshotBtnPressTime;
  bool screenshotPending;
};

const int16_t PRESSURE_VALUES_LEN = 160;

// Data structure for UI rendering
struct UIData {
  int16_t pressureValues[PRESSURE_VALUES_LEN];
  const int16_t *pressureHistory;
  const int16_t *weightHistory;
  const uint32_t *pressureHistoryTimes;
  size_t pressureHistoryCount;
  size_t weightHistoryCount;
  int16_t lastPressure;
  std::string hexData;
  bool isBluetoothOn;
  bool lastBTSendSuccessful;
  int32_t batteryLevel;
  bool debugMode;
  unsigned long shotTotalTime;
  int16_t displayWidth;
  int16_t displayHeight;
  int16_t maxPressure;
  bool deviceConnected;
  float shotWeight;  // in grams
  float flowRate;           // in grams/second
  bool scaleConnected;
  std::string scaleName;
  std::string nearbyScales;
};
