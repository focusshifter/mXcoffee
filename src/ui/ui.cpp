#include "ui.h"
#include <vector>
#include <string>
#include "utils/profiler.h"
#include <string>

#ifdef SIMULATOR
#include "SimulatorArduino.h"
#else
#include <Arduino.h>
#include <M5GFX.h>
#endif

#include "fonts/MoonGloss_16.h"
#include "fonts/MoonGloss_48.h"

const int16_t PRESSURE_GRID_VALUES[] = {9, 6, 3, 0};
const int16_t PRESSURE_GRID_COUNT = 4;

void UI::draw(const UIData &data) {
  Profiler p("UI::draw");
  const int16_t graphStartX = 10;
  const int16_t graphStartY = 90;
  const int16_t graphHeight = data.displayHeight - graphStartY - 30;
  const int16_t graphWidth = data.displayWidth - graphStartX - 45;

  const int16_t audioBarWidth = 30;
  const int16_t audioBarX = data.displayWidth - audioBarWidth - 10;
  const int16_t audioBarY = 90;
  const int16_t audioBarHeight = graphHeight;

  canvas->fillSprite(THEME_DARKBG);

  // Bottom status bar
  canvas->fillRect(0, data.displayHeight - 20, data.displayWidth, 20, THEME_LIGHTACCENTBG);

  // // Bluetooth status
  // canvas->setFont(&MoonGloss_16);
  // if (data.isBluetoothOn) {
  //   canvas->setFontColor(data.lastBTSendSuccessful ? TFT_GREEN : TFT_RED);
  // } else {
  //   canvas->setFontColor(THEME_DARKTEXT);
  // }
  // canvas->setCursor(data.displayWidth - 10, 30);
  // canvas->printf("BT");

  // // Battery level
  // if (data.batteryLevel > 25) canvas->setFontColor(TFT_GREEN);
  // else if (data.batteryLevel > 15) canvas->setFontColor(TFT_YELLOW);
  // else if (data.batteryLevel > 5) canvas->setFontColor(TFT_ORANGE);
  // else canvas->setFontColor(TFT_RED);
  // std::string batteryStr = std::to_string(data.batteryLevel) + "%";
  // canvas->setCursor(data.displayWidth - 10, 10);
  // canvas->printf(batteryStr.c_str());

  // // Pressure grid lines
  // canvas->setFontColor(THEME_DARKTEXT);
  // for (int i = 0; i < PRESSURE_GRID_COUNT; i++) {
  //   int16_t pressureY = graphStartY + (10 - PRESSURE_GRID_VALUES[i]) * graphHeight / 10;
  //   canvas->drawLine(graphStartX, pressureY, graphStartX + graphWidth, pressureY, THEME_GRID_LINES);
  //   std::string gridStr = std::to_string(PRESSURE_GRID_VALUES[i]);
  //   fontRenderer.setCursor(0, pressureY - 5);
  //   fontRenderer.setAlignment(Align::TopLeft);
  //   fontRenderer.printf(gridStr.c_str());
  // }

  // Pressure graph
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

  

  // Pressure audio bar
  int16_t audioBarHeightCurrent = std::min(
      static_cast<int16_t>((data.lastPressure) * audioBarHeight / (maxPressure)),
      audioBarHeight);
  canvas->fillRect(audioBarX, audioBarY, audioBarWidth, audioBarHeight + 1, THEME_BAR_BG);
  for (int16_t y = 0; y < audioBarHeightCurrent; y++) {
    int16_t pressureAtY = (y * (maxPressure) / audioBarHeight);
    uint32_t lineColor = (pressureAtY <= 6000) ? canvas->color888((pressureAtY * (96 - 64) / 6000) + 64, (pressureAtY * (96 - 64) / 6000) + 64, (pressureAtY * (96 - 64) / 6000) + 64) :
                         (pressureAtY <= 7000) ? canvas->color888(96 - ((pressureAtY - 6000) * 96 / 1000), 96 + ((pressureAtY - 6000) * (255 - 96) / 1000), 96 - ((pressureAtY - 6000) * 96 / 1000)) :
                         (pressureAtY <= 8000) ? TFT_GREEN :
                         (pressureAtY <= 8500) ? canvas->color888((pressureAtY - 8000) * 255 / 500, 255, 0) :
                         (pressureAtY <= 10000) ? canvas->color888(255, 255 - ((pressureAtY - 8500) * 255 / 1500), 0) : TFT_RED;
    canvas->drawFastHLine(audioBarX, audioBarY + audioBarHeight - y, audioBarWidth, lineColor);
  }

  // // Shot timer
  // fontRenderer.setFontSize(24);
  // fontRenderer.setFontColor(THEME_LIGHTTEXT);
  // char shotBuffer[16];
  // snprintf(shotBuffer, sizeof(shotBuffer), "%.1fs", float(data.shotTotalTime) / 1000);
  // fontRenderer.setCursor(130, 30);
  // fontRenderer.setAlignment(Align::TopRight);
  // fontRenderer.printf(shotBuffer);

  canvas->loadFont(MoonGloss_16);

  // Pressure panel
  canvas->fillRect(220, 0, 100, 80, THEME_PANEL_OUTER_BG);
  canvas->fillRect(222, 18, 96, 60, THEME_PANEL_INNER_BG);
  canvas->setTextColor(THEME_PANEL_HEADER_TEXT, THEME_PANEL_OUTER_BG);
  canvas->drawString("PRESSURE", 222, 2);

  // Shot timer panel
  canvas->fillRect(0, 0, 100, 80, THEME_PANEL_OUTER_BG);
  canvas->fillRect(2, 18, 96, 60, THEME_PANEL_INNER_BG);
  canvas->setTextColor(THEME_PANEL_HEADER_TEXT, THEME_PANEL_OUTER_BG);
  canvas->drawString("SHOT TIME", 2, 2);

  // Weight panel
  canvas->fillRect(110, 0, 100, 80, THEME_PANEL_OUTER_BG);
  canvas->fillRect(112, 18, 96, 60, THEME_PANEL_INNER_BG);
  canvas->setTextColor(THEME_PANEL_HEADER_TEXT, THEME_PANEL_OUTER_BG);
  canvas->drawString("WEIGHT G", 112, 2);

  // Panel counters
  canvas->setTextColor(THEME_PANEL_TEXT, THEME_PANEL_INNER_BG);
  
  canvas->loadFont(MoonGloss_16);
  String weightStr = String(data.shotWeight) + "g";
  canvas->drawRightString(weightStr, 200, 30);
  String flowStr = String(data.flowRate, 1) + " g/s";
  canvas->drawRightString(flowStr, 200, 50);
  
  canvas->loadFont(MoonGloss_48);

  // Timer text
  canvas->drawRightString(String((float(data.shotTotalTime) / 1000), 1), 92, 25);
  // Pressure text
  canvas->drawRightString(String((float(data.lastPressure) / 1000), 1), 312, 25);



  // // Pressure warning
  // if (showPressureWarning) {
  //   canvas->setFont(&MoonGloss_16);
  //   canvas->setFontColor(TFT_RED);
  //   canvas->setCursor(160, 120);
  //   canvas->printf("STOP!");
  // }

  // // Debug mode
  // if (data.debugMode) {
  //   canvas->setFont(&MoonGloss_16);
  //   canvas->setFontColor(THEME_LIGHTTEXT);
  //   std::vector<std::string> debugStrings = {
  //       "Pressure: " + std::to_string(data.lastPressure),
  //       "Hex: " + data.hexData,
  //       std::string("BT: ") + (data.isBluetoothOn ? "ON" : "OFF"),
  //       std::string("Connected: ") + (data.deviceConnected ? "YES" : "NO")
  //   };
  //   for (int i = 0; i < debugStrings.size(); i++) {
  //       canvas->setCursor(40, 60 + i * 20);
  //       canvas->printf(debugStrings[i].c_str());
  //   }
  // }

  canvas->pushSprite(0, 0);
  Serial.println("Draw: Pushed sprite");
}
