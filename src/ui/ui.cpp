#include "ui.h"
#include <vector>
#include <string>
#include <OpenFontRender.h>  // Add OpenFontRender

#ifdef SIMULATOR
#include "SimulatorArduino.h"
#else
#include <Arduino.h>
#include <M5GFX.h>
#endif

extern OpenFontRender fontRenderer;  // Declare the global renderer

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

  canvas->fillSprite(TFT_BLACK);

  // Bluetooth status
  fontRenderer.setFontSize(12);  // Small size for status
  if (data.isBluetoothOn) {
    fontRenderer.setFontColor(data.lastBTSendSuccessful ? TFT_GREEN : TFT_RED);
  } else {
    fontRenderer.setFontColor(TFT_DARKGRAY);
  }
  fontRenderer.drawString("BT", data.displayWidth - 10, 30);

  // Battery level
  if (data.batteryLevel > 25) fontRenderer.setFontColor(TFT_GREEN);
  else if (data.batteryLevel > 15) fontRenderer.setFontColor(TFT_YELLOW);
  else if (data.batteryLevel > 5) fontRenderer.setFontColor(TFT_ORANGE);
  else fontRenderer.setFontColor(TFT_RED);
  std::string batteryStr = std::to_string(data.batteryLevel) + "%";
  fontRenderer.drawString(batteryStr.c_str(), data.displayWidth - 10, 10);

  // Pressure grid lines
  fontRenderer.setFontColor(TFT_DARKGREY);
  const uint16_t TFT_VERY_DARK_GRAY = canvas->color565(20, 20, 20);
  for (int i = 0; i < PRESSURE_GRID_COUNT; i++) {
    int16_t pressureY = graphStartY + (10 - PRESSURE_GRID_VALUES[i]) * graphHeight / 10;
    canvas->drawLine(graphStartX, pressureY, graphStartX + graphWidth, pressureY, TFT_VERY_DARK_GRAY);
    std::string gridStr = std::to_string(PRESSURE_GRID_VALUES[i]);
    fontRenderer.drawString(gridStr.c_str(), 0, pressureY - 5);
  }

  // Pressure graph (unchanged)
  int graphColor = (data.lastPressure > 12000) ? TFT_RED : (data.lastPressure > 9000) ? TFT_YELLOW : TFT_GREEN;
  bool showPressureWarning = data.lastPressure > 12000;
  int16_t maxPressure = data.maxPressure - 10000;
  for (int i = 1; i < PRESSURE_VALUES_LEN; i++) {
    int16_t pressure1 = data.pressureValues[i - 1];
    int16_t pressure2 = data.pressureValues[i];
    int16_t pressureY1 = graphStartY + graphHeight - pressure1 * graphHeight / maxPressure;
    int16_t pressureY2 = graphStartY + graphHeight - pressure2 * graphHeight / maxPressure;
    int16_t pressureX1 = graphStartX + (i - 1) * graphWidth / PRESSURE_VALUES_LEN;
    int16_t pressureX2 = graphStartX + i * graphWidth / PRESSURE_VALUES_LEN;
    canvas->drawLine(pressureX1, pressureY1, pressureX2, pressureY2, graphColor);
  }

  // Pressure value
  fontRenderer.setFontSize(16);  // Larger for readability
  fontRenderer.setFontColor(graphColor);
  std::string pressureStr = std::to_string(float(data.lastPressure) / 1000) + " bar";
  fontRenderer.drawString(pressureStr.c_str(), 260, 10);

  // Audio bar (unchanged)
  int16_t audioBarHeightCurrent = std::min(
      static_cast<int16_t>((data.lastPressure) * audioBarHeight / (maxPressure)),
      audioBarHeight);
  canvas->fillRect(audioBarX, audioBarY, audioBarWidth, audioBarHeight + 1, TFT_VERY_DARK_GRAY);
  for (int16_t y = 0; y < audioBarHeightCurrent; y++) {
    int16_t pressureAtY = (y * (maxPressure) / audioBarHeight);
    uint16_t lineColor = (pressureAtY <= 6000) ? canvas->color565((pressureAtY * (96 - 64) / 6000) + 64, (pressureAtY * (96 - 64) / 6000) + 64, (pressureAtY * (96 - 64) / 6000) + 64) :
                         (pressureAtY <= 7000) ? canvas->color565(96 - ((pressureAtY - 6000) * 96 / 1000), 96 + ((pressureAtY - 6000) * (255 - 96) / 1000), 96 - ((pressureAtY - 6000) * 96 / 1000)) :
                         (pressureAtY <= 8000) ? TFT_GREEN :
                         (pressureAtY <= 8500) ? canvas->color565((pressureAtY - 8000) * 255 / 500, 255, 0) :
                         (pressureAtY <= 10000) ? canvas->color565(255, 255 - ((pressureAtY - 8500) * 255 / 1500), 0) : TFT_RED;
    canvas->drawFastHLine(audioBarX, audioBarY + audioBarHeight - y, audioBarWidth, lineColor);
  }

  // Shot timer
  fontRenderer.setFontSize(12);
  fontRenderer.setFontColor(TFT_WHITE);
  float shotTime = float(data.shotTotalTime) / 1000;
  std::string shotTimeStr = std::to_string(shotTime) + "s";
  fontRenderer.drawString(shotTimeStr.c_str(), 130, 10);

  // Pressure warning
  if (showPressureWarning) {
    fontRenderer.setFontSize(24);  // Larger for emphasis
    fontRenderer.setFontColor(TFT_RED);
    fontRenderer.drawString("STOP!", 160, 120);
  }

  // Debug mode
  if (data.debugMode) {
      fontRenderer.setFontSize(12);
      fontRenderer.setFontColor(TFT_WHITE);
      std::vector<std::string> debugStrings = {
          "Pressure: " + std::to_string(data.lastPressure),
          "Hex: " + data.hexData,
          std::string("BT: ") + (data.isBluetoothOn ? "ON" : "OFF"),
          std::string("Connected: ") + (data.deviceConnected ? "YES" : "NO")
      };
      for (int i = 0; i < debugStrings.size(); i++) {
          fontRenderer.drawString(debugStrings[i].c_str(), 40, 60 + i * 20);
      }
  }

  canvas->pushSprite(0, 0);
  Serial.println("Draw: Pushed sprite");
}