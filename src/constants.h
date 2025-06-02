#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#ifdef SIMULATOR
#define TFT_WHITE 0xFFFF
#define TFT_RED 0xF800
#define TFT_ORANGE 0xFBE0
#define TFT_YELLOW 0xFFE0
#define TFT_GREEN 0x07E0
#define TFT_CYAN 0x07FF
#define TFT_BLUE 0x001F
#define TFT_PURPLE 0x781F
#define TFT_BLACK 0x0000
#define TFT_DARKGRAY 0x7BEF
#define TFT_DARKGREY TFT_DARKGRAY
#endif

#define SMOOTH_FONT


// Color theme
#define THEME_LIGHTBG 0x9E78
#define THEME_LIGHTTEXT 0xE7FF

#define THEME_DARKBG 0x03161E
#define THEME_DARKTEXT 0x0986

#define THEME_LIGHTACCENTBG 0x96CBBB
#define THEME_LIGHTACCENTTEXT 0xA3B44

// #define THEME_DARKACCENTBG 0x7AC8
// #define THEME_DARKACCENTTEXT 0x7AC8

#define THEME_DISABLEDBG 0x7A26
#define THEME_DISABLEDTEXT 0x3124

#define THEME_GRAPH_GOOD 0xB7FE
#define THEME_GRAPH_WARNING 0xD54F
#define THEME_GRAPH_BAD 0xD54F

#define THEME_GRID_LINES 0x0A4A

#define THEME_BAR_BG 0xA3B44

// Panels

#define THEME_PANEL_OUTER_BG 0x96CBBB
#define THEME_PANEL_HEADER_TEXT 0xA3B44
#define THEME_PANEL_TEXT 0xDDFEEE
#define THEME_PANEL_INNER_BG 0xA3B44

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
  int16_t lastPressure;
  unsigned long timerStartTime;
  unsigned long shotTotalTime;
  bool isTimerRunning;
  void *pServer;

  unsigned long shotWeight;     // in grams
  unsigned long lastShotWeight;  // Previous weight for flow rate calculation
  float flowRate;               // in grams/second
};

const int16_t PRESSURE_VALUES_LEN = 160;

// Data structure for UI rendering
struct UIData {
  int16_t pressureValues[PRESSURE_VALUES_LEN];
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
  unsigned long shotWeight;  // in grams
  float flowRate;           // in grams/second
};
