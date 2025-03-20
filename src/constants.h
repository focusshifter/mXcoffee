#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#ifdef SIMULATOR
#define PROGMEM
typedef struct {
  uint16_t bitmapOffset;
  uint8_t width;
  uint8_t height;
  uint8_t xAdvance;
  int8_t xOffset;
  int8_t yOffset;
} GFXglyph;

typedef struct {
  uint8_t *bitmap;
  GFXglyph *glyph;
  uint16_t first;
  uint16_t last;
  uint8_t yAdvance;
} GFXfont;

// Include DejaVu fonts directly
#include "fonts/DejaVu12.h"
#include "fonts/DejaVu24.h"
#include "fonts/DejaVu56.h"
#else
// For M5Unified, use M5GFX fonts but ensure compatibility
#include <M5GFX.h>
#endif

#include "fonts/Dosis_Medium9pt7b.h"
#include "fonts/Dosis_Medium12pt7b.h"
#include "fonts/Dosis_Medium16pt7b.h"
#include "fonts/Dosis_Medium24pt7b.h"

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

const GFXfont FontBase9 = Dosis_Medium9pt7b;
const GFXfont FontBase12 = Dosis_Medium12pt7b;
const GFXfont FontBase16 = Dosis_Medium16pt7b;
const GFXfont FontBase24 = Dosis_Medium24pt7b;