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

// Color theme
#define THEME_LIGHTBG 0x9E78
#define THEME_LIGHTTEXT 0xE7FF

#define THEME_DARKBG 0x0105
#define THEME_DARKTEXT 0x0986

#define THEME_LIGHTACCENTBG 0x9E77
#define THEME_LIGHTACCENTTEXT 0x0986

// #define THEME_DARKACCENTBG 0x7AC8
// #define THEME_DARKACCENTTEXT 0x7AC8

#define THEME_DISABLEDBG 0x7A26
#define THEME_DISABLEDTEXT 0x3124

#define THEME_GRAPH_GOOD 0xB7FE
#define THEME_GRAPH_WARNING 0xD54F
#define THEME_GRAPH_BAD 0xD54F

#define THEME_GRID_LINES 0x0A4A

#define THEME_BAR_BG 0x0A4A


struct DeviceState {
  bool isAsleep;
  bool isBluetoothOn;
  bool deviceConnected;
  bool lastBTSendSuccessful;
  bool debugMode;
  unsigned long lastRefreshTime;
  unsigned long lastActivityTime;
  int16_t lastPressure;
  unsigned long timerStartTime;
  unsigned long shotTotalTime;
  bool isTimerRunning;
  void *pServer;
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
  bool deviceConnected; // Added to match ui.cpp usage
};
