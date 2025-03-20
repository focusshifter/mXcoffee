#include "ui.h"
#include <vector>

#ifdef SIMULATOR
#include "SimulatorArduino.h"
#else
#include <Arduino.h>
#include <M5GFX.h>
#endif

const int16_t PRESSURE_GRID_VALUES[] = {9, 6, 3, 0};
const int16_t PRESSURE_GRID_COUNT = 4;

void UI::draw(const UIData &data) {
  const int16_t graphStartX = 10;
  const int16_t graphStartY = 60;
  const int16_t graphHeight = data.displayHeight - graphStartY - 10;
  const int16_t graphWidth = data.displayWidth - graphStartX - 45;

  const int16_t audioBarWidth = 30;
  const int16_t audioBarX = data.displayWidth - audioBarWidth - 10;
  const int16_t audioBarY = 60;
  const int16_t audioBarHeight = graphHeight;

  static bool initialized = false;
  if (!initialized) {
    canvas->createSprite(data.displayWidth, data.displayHeight);
    initialized = true;
  }

  canvas->fillSprite(TFT_BLACK);
#ifdef SIMULATOR
  Serial_println("Draw: Filled sprite");
#else
  Serial.println("Draw: Filled sprite");
#endif

  canvas->setFont(&DejaVu12);
#ifdef SIMULATOR
  Serial_println("Draw: Set initial font");
#else
  Serial.println("Draw: Set initial font");
#endif

  // Bluetooth status
  if (data.isBluetoothOn) {
    canvas->setTextColor(data.lastBTSendSuccessful ? TFT_GREEN : TFT_RED,
                         TFT_BLACK);
  } else {
    canvas->setTextColor(TFT_DARKGRAY, TFT_BLACK);
  }
  canvas->drawRightString("BT", data.displayWidth - 10, 30);
#ifdef SIMULATOR
  Serial_println("Draw: Drew BT status");
#else
  Serial.println("Draw: Drew BT status");
#endif

  // Battery level
  if (data.batteryLevel > 25)
    canvas->setTextColor(TFT_GREEN, TFT_BLACK);
  else if (data.batteryLevel > 15)
    canvas->setTextColor(TFT_YELLOW, TFT_BLACK);
  else if (data.batteryLevel > 5)
    canvas->setTextColor(TFT_ORANGE, TFT_BLACK);
  else
    canvas->setTextColor(TFT_RED, TFT_BLACK);
  std::string batteryStr = std::to_string(data.batteryLevel) + "%";
  canvas->drawRightString(batteryStr.c_str(), data.displayWidth - 10, 10);
#ifdef SIMULATOR
  Serial_println("Draw: Drew battery level");
#else
  Serial.println("Draw: Drew battery level");
#endif

  // Pressure grid lines
  canvas->setTextColor(TFT_DARKGREY, TFT_BLACK);
  const uint16_t TFT_VERY_DARK_GRAY = canvas->color565(20, 20, 20);
  for (int i = 0; i < PRESSURE_GRID_COUNT; i++) {
    int16_t pressureY =
        graphStartY + (10 - PRESSURE_GRID_VALUES[i]) * graphHeight / 10;
    canvas->drawLine(graphStartX, pressureY, graphStartX + graphWidth,
                     pressureY, TFT_VERY_DARK_GRAY);
    std::string gridStr = std::to_string(PRESSURE_GRID_VALUES[i]);
    canvas->drawString(gridStr.c_str(), 0, pressureY - 5);
  }
#ifdef SIMULATOR
  Serial_println("Draw: Drew pressure grid");
#else
  Serial.println("Draw: Drew pressure grid");
#endif

  // Pressure graph
  int graphColor;
  bool showPressureWarning = false;
  if (data.lastPressure > 12000) {
    graphColor = TFT_RED;
    showPressureWarning = true;
  } else if (data.lastPressure > 9000) {
    graphColor = TFT_YELLOW;
  } else {
    graphColor = TFT_GREEN;
  }
  int16_t minPressure = 0;
  int16_t maxPressure = data.maxPressure - 10000;
  for (int i = 1; i < PRESSURE_VALUES_LEN; i++) {
    int16_t pressure1 = data.pressureValues[i - 1];
    int16_t pressure2 = data.pressureValues[i];
    int16_t pressureY1 =
        graphStartY + graphHeight - pressure1 * graphHeight / maxPressure;
    int16_t pressureY2 =
        graphStartY + graphHeight - pressure2 * graphHeight / maxPressure;
    int16_t pressureX1 =
        graphStartX + (i - 1) * graphWidth / PRESSURE_VALUES_LEN;
    int16_t pressureX2 = graphStartX + i * graphWidth / PRESSURE_VALUES_LEN;
    canvas->drawLine(pressureX1, pressureY1, pressureX2, pressureY2,
                     graphColor);
  }
#ifdef SIMULATOR
  Serial_println("Draw: Drew pressure graph");
#else
  Serial.println("Draw: Drew pressure graph");
#endif

  // Pressure value
  canvas->setFont(&DejaVu24);
  canvas->setTextColor(graphColor, TFT_BLACK);
  std::string pressureStr =
      std::to_string(float(data.lastPressure) / 1000) + " bar";
  canvas->drawRightString(pressureStr.c_str(), 260, 10);
#ifdef SIMULATOR
  Serial_println("Draw: Drew pressure value");
#else
  Serial.println("Draw: Drew pressure value");
#endif

  // Audio bar
  int16_t audioBarHeightCurrent = std::min(
      static_cast<int16_t>((data.lastPressure - minPressure) * audioBarHeight /
                           (maxPressure - minPressure)),
      audioBarHeight);
  canvas->fillRect(audioBarX, audioBarY, audioBarWidth, audioBarHeight + 1,
                   TFT_VERY_DARK_GRAY);
  for (int16_t y = 0; y < audioBarHeightCurrent; y++) {
    int16_t pressureAtY =
        (y * (maxPressure - minPressure) / audioBarHeight) + minPressure;
    uint16_t lineColor;
    if (pressureAtY <= 6000) {
      uint8_t grayComponent = (pressureAtY * (96 - 64) / 6000) + 64;
      lineColor = canvas->color565(grayComponent, grayComponent, grayComponent);
    } else if (pressureAtY <= 7000) {
      uint8_t greyComponent = 96 - ((pressureAtY - 6000) * 96 / 1000);
      uint8_t greenComponent = 96 + ((pressureAtY - 6000) * (255 - 96) / 1000);
      lineColor =
          canvas->color565(greyComponent, greenComponent, greyComponent);
    } else if (pressureAtY <= 8000) {
      lineColor = canvas->color565(0, 255, 0);
    } else if (pressureAtY <= 8500) {
      uint8_t redComponent = (pressureAtY - 8000) * 255 / 500;
      lineColor = canvas->color565(redComponent, 255, 0);
    } else if (pressureAtY <= 10000) {
      uint8_t greenComponent = 255 - ((pressureAtY - 8500) * 255 / 1500);
      lineColor = canvas->color565(255, greenComponent, 0);
    } else {
      lineColor = canvas->color565(255, 0, 0);
    }
    canvas->drawFastHLine(audioBarX, audioBarY + audioBarHeight - y,
                          audioBarWidth, lineColor);
  }
#ifdef SIMULATOR
  Serial_println("Draw: Drew audio bar");
#else
  Serial.println("Draw: Drew audio bar");
#endif

  // Shot timer
  canvas->setFont(&DejaVu12);
  canvas->setTextColor(TFT_WHITE, TFT_BLACK);
  float shotTime = float(data.shotTotalTime) / 1000;
  std::string shotTimeStr = std::to_string(shotTime) + "s";
  canvas->drawRightString(shotTimeStr.c_str(), 130, 10);
#ifdef SIMULATOR
  Serial_println("Draw: Drew shot timer");
#else
  Serial.println("Draw: Drew shot timer");
#endif

  // Pressure warning
  if (showPressureWarning) {
    canvas->setFont(&DejaVu56);
    canvas->setTextColor(TFT_RED, TFT_BLACK);
    canvas->drawCenterString("STOP!", 160, 120);
#ifdef SIMULATOR
    Serial_println("Draw: Drew pressure warning");
#else
    Serial.println("Draw: Drew pressure warning");
#endif
  }

  // Debug mode
  if (data.debugMode) {
    canvas->setFont(&DejaVu12);
    canvas->setTextColor(TFT_WHITE, TFT_BLACK);
    std::vector<std::string> debugStrings = {
        std::string("Pressure: ") + std::to_string(data.lastPressure),
        std::string("Hex: ") + data.hexData,
        std::string("BT: ") + (data.isBluetoothOn ? "ON" : "OFF"),
        std::string("Connected: ") + (data.deviceConnected ? "YES" : "NO")};
    for (int i = 0; i < debugStrings.size(); i++) {
      canvas->drawString(debugStrings[i].c_str(), 40, 60 + i * 20);
    }
#ifdef SIMULATOR
    Serial_println("Draw: Drew debug info");
#else
    Serial.println("Draw: Drew debug info");
#endif
  }

  canvas->pushSprite(0, 0);
#ifdef SIMULATOR
  Serial_println("Draw: Pushed sprite");
#else
  Serial.println("Draw: Pushed sprite");
#endif
}