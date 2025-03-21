#include "ui.h"
#include <vector>
#include <string>
#include <OpenFontRender.h>  // Add OpenFontRender

#include "utils/profiler.h"

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
  Profiler p("UI::draw");
  const int16_t graphStartX = 10;
  const int16_t graphStartY = 60;
  const int16_t graphHeight = data.displayHeight - graphStartY - 30;
  const int16_t graphWidth = data.displayWidth - graphStartX - 45;

  const int16_t audioBarWidth = 30;
  const int16_t audioBarX = data.displayWidth - audioBarWidth - 10;
  const int16_t audioBarY = 60;
  const int16_t audioBarHeight = graphHeight;

  canvas->fillSprite(THEME_DARKBG);

  // Bottom status bar
  canvas->fillRect(0, data.displayHeight - 20, data.displayWidth, 20, THEME_LIGHTACCENTBG);

  // Bluetooth status
  fontRenderer.setFontSize(12);  // Small size for status
  if (data.isBluetoothOn) {
    fontRenderer.setFontColor(data.lastBTSendSuccessful ? TFT_GREEN : TFT_RED);
  } else {
    fontRenderer.setFontColor(THEME_DARKTEXT);
  }
  fontRenderer.setCursor(data.displayWidth - 10, 30);
  fontRenderer.setAlignment(Align::TopRight);
  fontRenderer.printf("BT");

  // Battery level
  if (data.batteryLevel > 25) fontRenderer.setFontColor(TFT_GREEN);
  else if (data.batteryLevel > 15) fontRenderer.setFontColor(TFT_YELLOW);
  else if (data.batteryLevel > 5) fontRenderer.setFontColor(TFT_ORANGE);
  else fontRenderer.setFontColor(TFT_RED);
  std::string batteryStr = std::to_string(data.batteryLevel) + "%";
  fontRenderer.setCursor(data.displayWidth - 10, 10);
  fontRenderer.setAlignment(Align::TopRight);
  fontRenderer.printf(batteryStr.c_str());

  // Pressure grid lines
  fontRenderer.setFontColor(THEME_DARKTEXT);
  for (int i = 0; i < PRESSURE_GRID_COUNT; i++) {
    int16_t pressureY = graphStartY + (10 - PRESSURE_GRID_VALUES[i]) * graphHeight / 10;
    canvas->drawLine(graphStartX, pressureY, graphStartX + graphWidth, pressureY, THEME_GRID_LINES);
    std::string gridStr = std::to_string(PRESSURE_GRID_VALUES[i]);
    fontRenderer.setCursor(0, pressureY - 5);
    fontRenderer.setAlignment(Align::TopLeft);
    fontRenderer.printf(gridStr.c_str());
  }

  // Pressure graph (unchanged)
  int graphColor = (data.lastPressure > 12000) ? THEME_GRAPH_BAD : (data.lastPressure > 9000) ? THEME_GRAPH_WARNING : THEME_GRAPH_GOOD;
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
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%.1f bar", float(data.lastPressure) / 1000);
  fontRenderer.setCursor(260, 10);
  fontRenderer.setAlignment(Align::TopRight);
  fontRenderer.printf(buffer);

  // Audio bar (unchanged)
  int16_t audioBarHeightCurrent = std::min(
      static_cast<int16_t>((data.lastPressure) * audioBarHeight / (maxPressure)),
      audioBarHeight);
  canvas->fillRect(audioBarX, audioBarY, audioBarWidth, audioBarHeight + 1, THEME_BAR_BG);
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
  fontRenderer.setFontSize(24);
  fontRenderer.setFontColor(THEME_LIGHTTEXT);
  char shotBuffer[16];
  snprintf(shotBuffer, sizeof(shotBuffer), "%.1fs", float(data.shotTotalTime) / 1000);
  fontRenderer.setCursor(130, 30);
  fontRenderer.setAlignment(Align::TopRight);
  fontRenderer.printf(shotBuffer);


  // Pressure warning
  if (showPressureWarning) {
    fontRenderer.setFontSize(24);  // Larger for emphasis
    fontRenderer.setFontColor(TFT_RED);
    fontRenderer.setCursor(160, 120);
    fontRenderer.setAlignment(Align::TopRight);
    fontRenderer.printf("STOP!");
  }

  // Debug mode
  if (data.debugMode) {
      fontRenderer.setFontSize(12);
      fontRenderer.setFontColor(THEME_LIGHTTEXT);
      std::vector<std::string> debugStrings = {
          "Pressure: " + std::to_string(data.lastPressure),
          "Hex: " + data.hexData,
          std::string("BT: ") + (data.isBluetoothOn ? "ON" : "OFF"),
          std::string("Connected: ") + (data.deviceConnected ? "YES" : "NO")
      };
      for (int i = 0; i < debugStrings.size(); i++) {
          fontRenderer.setCursor(40, 60 + i * 20);
          fontRenderer.setAlignment(Align::TopLeft);
          fontRenderer.printf(debugStrings[i].c_str());
      }
  }

  canvas->pushSprite(0, 0);
  Serial.println("Draw: Pushed sprite");
}