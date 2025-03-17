#include "ui.h"

#include <M5GFX.h>
#include <M5Unified.h>

#include <vector>

#include "../constants.h"
#include "../pressure_sensor/pressure_sensor.h"

void UI::drawGraph()
{
  const int16_t graphStartX = 10;
  const int16_t graphStartY = 60;
  const int16_t graphHeight = sysWrapper->displayHeight() - graphStartY - 10;
  const int16_t graphWidth = sysWrapper->displayWidth() - graphStartX - 45;

  const int16_t audioBarWidth = 30;
  const int16_t audioBarX = sysWrapper->displayWidth() - audioBarWidth - 10;
  const int16_t audioBarY = 60;
  const int16_t audioBarHeight = graphHeight;

  static bool initialized = false;
  if (!initialized)
  {
    canvas->createSprite(sysWrapper->displayWidth(), sysWrapper->displayHeight());
    initialized = true;
  }

  int16_t lastPressure = pressureValues[PRESSURE_VALUES_LEN - 1];
  String hex_data = pressureSensor->getHexData();

  int graphColor;
  bool showPressureWarning = false;
  if (lastPressure > 12000)
  {
    graphColor = TFT_RED;
    showPressureWarning = true;
  }
  else if (lastPressure > 9000)
  {
    graphColor = TFT_YELLOW;
  }
  else
  {
    graphColor = TFT_GREEN;
  }

  int16_t minPressure = 0;
  int16_t maxSensorPressure = pressureSensor->getMaxPressure();
  int16_t maxPressure = maxSensorPressure - 10000;

  int16_t minPressureY = graphStartY + graphHeight;
  int16_t maxPressureY = graphStartY;

  canvas->fillSprite(TFT_BLACK);

  canvas->setFont(&Font2);

  if (deviceState.isBluetoothOn)
  {
    canvas->setTextColor(deviceState.lastBTSendSuccessful ? TFT_GREEN : TFT_RED, TFT_BLACK);
  }
  else
  {
    canvas->setTextColor(TFT_DARKGRAY, TFT_BLACK);
  }
  canvas->drawRightString("BT", sysWrapper->displayWidth() - 10, 30);

  int32_t batteryLevel = sysWrapper->getBatteryLevel();
  if (batteryLevel > 25)
    canvas->setTextColor(TFT_GREEN, TFT_BLACK);
  else if (batteryLevel > 15)
    canvas->setTextColor(TFT_YELLOW, TFT_BLACK);
  else if (batteryLevel > 5)
    canvas->setTextColor(TFT_ORANGE, TFT_BLACK);
  else
    canvas->setTextColor(TFT_RED, TFT_BLACK);
  String batteryStr = String(batteryLevel) + "%";
  canvas->drawRightString(batteryStr.c_str(), sysWrapper->displayWidth() - 10, 10);

  canvas->setTextColor(TFT_DARKGREY, TFT_BLACK);
  const uint16_t TFT_VERY_DARK_GRAY = canvas->color565(20, 20, 20);

  for (int i = 0; i < PRESSURE_GRID_COUNT; i++)
  {
    int16_t pressureY = graphStartY + (10 - PRESSURE_GRID_VALUES[i]) * graphHeight / 10;
    canvas->drawLine(graphStartX, pressureY, graphStartX + graphWidth, pressureY,
                     TFT_VERY_DARK_GRAY);
    String gridStr = String(PRESSURE_GRID_VALUES[i]);
    canvas->drawString(gridStr.c_str(), 0, pressureY - 5);
  }

  for (int i = 1; i < PRESSURE_VALUES_LEN; i++)
  {
    int16_t pressure1 = pressureValues[i - 1];
    int16_t pressure2 = pressureValues[i];
    int16_t pressureY1 = graphStartY + graphHeight - pressure1 * graphHeight / maxPressure;
    int16_t pressureY2 = graphStartY + graphHeight - pressure2 * graphHeight / maxPressure;
    int16_t pressureX1 = graphStartX + (i - 1) * graphWidth / PRESSURE_VALUES_LEN;
    int16_t pressureX2 = graphStartX + i * graphWidth / PRESSURE_VALUES_LEN;
    canvas->drawLine(pressureX1, pressureY1, pressureX2, pressureY2, graphColor);
  }

  canvas->setFont(&Font4);
  canvas->setTextColor(graphColor, TFT_BLACK);
  String pressureStr = String(float(lastPressure) / 1000, 1);
  canvas->drawRightString(pressureStr.c_str(), 260, 10);

  int16_t audioBarHeightCurrent =
      min(static_cast<int16_t>((lastPressure - minPressure) * audioBarHeight /
                               (maxPressure - minPressure)),
          audioBarHeight);
  int16_t audioBarYCurrent = graphStartY + graphHeight - audioBarHeightCurrent;
  canvas->fillRect(audioBarX, audioBarY, audioBarWidth, audioBarHeight + 1, TFT_VERY_DARK_GRAY);

  for (int16_t y = 0; y < audioBarHeightCurrent; y++)
  {
    int16_t pressureAtY = map(y, 0, audioBarHeight, minPressure, maxPressure);
    uint16_t lineColor;
    if (pressureAtY <= 6000)
    {
      uint8_t grayComponent = map(pressureAtY, 0, 6000, 64, 96);
      lineColor = canvas->color565(grayComponent, grayComponent, grayComponent);
    }
    else if (pressureAtY <= 7000)
    {
      uint8_t greyComponent = map(pressureAtY, 6000, 7000, 96, 0);
      uint8_t greenComponent = map(pressureAtY, 6000, 7000, 96, 255);
      lineColor = canvas->color565(greyComponent, greenComponent, greyComponent);
    }
    else if (pressureAtY <= 8000)
    {
      lineColor = canvas->color565(0, 255, 0);
    }
    else if (pressureAtY <= 8500)
    {
      uint8_t redComponent = map(pressureAtY, 8000, 8500, 0, 255);
      lineColor = canvas->color565(redComponent, 255, 0);
    }
    else if (pressureAtY <= 10000)
    {
      uint8_t greenComponent = map(pressureAtY, 8500, 10000, 255, 0);
      lineColor = canvas->color565(255, greenComponent, 0);
    }
    else
    {
      lineColor = canvas->color565(255, 0, 0);
    }
    canvas->drawFastHLine(audioBarX, audioBarY + audioBarHeight - y, audioBarWidth, lineColor);
  }

  canvas->setTextColor(TFT_WHITE, TFT_BLACK);
  float shotTime = float(deviceState.shotTotalTime) / 1000;
  String shotTimeStr = String(shotTime, 1) + "s";
  canvas->drawRightString(shotTimeStr.c_str(), 130, 10);

  if (showPressureWarning)
  {
    canvas->setTextColor(TFT_RED, TFT_BLACK);
    canvas->setFont(&Font8);
    canvas->drawCenterString("STOP!", 160, 120);
  }

  if (deviceState.debugMode)
  {
    canvas->setTextColor(TFT_WHITE, TFT_BLACK);
    canvas->setFont(&Font2);
    std::vector<String> debugStrings = {
        "Pressure: " + String(lastPressure), "Hex: " + hex_data,
        "BT: " + String(deviceState.isBluetoothOn ? "ON" : "OFF"),
        "Connected: " + String(deviceState.deviceConnected ? "YES" : "NO")};
    for (int i = 0; i < debugStrings.size(); i++)
    {
      canvas->drawString(debugStrings[i].c_str(), 40, 60 + i * 20);
    }
  }

  canvas->pushSprite(0, 0);
}