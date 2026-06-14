#include "ui.h"
#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>
#include <string>
#include "utils/profiler.h"
#include <string>

#include <Arduino.h>
#include <M5GFX.h>
#include <FS.h>
#include <SPIFFS.h>
#include <cmath>

#include "fonts/MoonGloss_16.h"
#include "fonts/MoonGloss_24.h"
#include "fonts/MoonGloss_48.h"

const int16_t PRESSURE_GRID_VALUES[] = {9, 6, 3, 0};
const int16_t PRESSURE_GRID_COUNT = 4;

UI::UI(M5GFX *display) : display(display) {
  Serial.println("UI: Constructor called");

  canvas = new M5Canvas(display);
  if (!canvas) {
      Serial.println("UI: Failed to allocate M5Canvas!");
      while (1) delay(1000);
  }

  // Use external PSRAM for the sprite buffer when available
  canvas->setPsram(true);
  // We will create the sprite later in the first call to draw(), once the
  // display has been initialised by M5.begin() and we know its real size.
  canvas->pushSprite(0, 0);
}

UI::~UI() {
  if (canvas) {
      canvas->deleteSprite();
      delete canvas;
  }
}

void UI::drawSplash() {
  if (!display || !canvas) {
    return;
  }
  if (canvas->width() == 0 || canvas->height() == 0) {
    canvas->setPsram(true);
    canvas->setColorDepth(24);
    canvas->setSwapBytes(false);
    canvas->createSprite(display->width(), display->height());
  }

  canvas->fillSprite(TFT_BLACK);
  canvas->loadFont(MoonGloss_24);
  canvas->setTextSize(1);
  canvas->setTextColor(THEME_LIGHTTEXT);

  int16_t fontHeight = canvas->fontHeight();
  int16_t xSize = static_cast<int16_t>(fontHeight * 1.1f);
  int16_t textWidthM = canvas->textWidth("m");
  int16_t textWidthCoffee = canvas->textWidth("coffee");
  int16_t totalWidth = textWidthM + xSize + textWidthCoffee;
  int16_t textY = (canvas->height() - fontHeight) / 2;
  int16_t startX = (canvas->width() - totalWidth) / 2;
  int16_t xCenter = startX + textWidthM + xSize / 2;
  int16_t coffeeX = startX + textWidthM + xSize;
  int16_t xY = textY + fontHeight / 2;
  const int16_t logoOffsetX = -1;
  const int16_t logoOffsetY = 2;
  xCenter += logoOffsetX;
  xY += logoOffsetY;
  int16_t radius = static_cast<int16_t>(xSize * 0.55f);

  canvas->drawString("m", startX, textY);
  canvas->drawString("coffee", coffeeX, textY);
  canvas->fillCircle(xCenter, xY, radius, lgfx::color888(0x5A, 0x3A, 0x1A));

  float length = radius * 0.6f;
  float curveAmp = radius * 0.45f;
  const int segments = 20;
  const float invSqrt2 = 0.70710678f;

  auto drawLatteStroke = [&](float baseX, float baseY, float perpX, float perpY) {
    for (int i = 0; i < segments; i++) {
      float t1 = -1.0f + 2.0f * (static_cast<float>(i) / segments);
      float t2 = -1.0f + 2.0f * (static_cast<float>(i + 1) / segments);
      float offset1 = sinf(t1 * 3.14159265f) * curveAmp;
      float offset2 = sinf(t2 * 3.14159265f) * curveAmp;
      float ax = t1 * length + offset1 * perpX;
      float ay = t1 * length + offset1 * perpY;
      float bx = t2 * length + offset2 * perpX;
      float by = t2 * length + offset2 * perpY;
      int16_t x1 = xCenter + static_cast<int16_t>(baseX * ax - baseY * ay);
      int16_t y1 = xY + static_cast<int16_t>(baseY * ax + baseX * ay);
      int16_t x2 = xCenter + static_cast<int16_t>(baseX * bx - baseY * by);
      int16_t y2 = xY + static_cast<int16_t>(baseY * bx + baseX * by);
      canvas->drawLine(x1, y1, x2, y2, lgfx::color888(0xF4, 0xE5, 0xC3));
      canvas->drawLine(x1 + 1, y1, x2 + 1, y2, lgfx::color888(0xF4, 0xE5, 0xC3));
    }
  };

  drawLatteStroke(invSqrt2, invSqrt2, -invSqrt2, invSqrt2);
  drawLatteStroke(invSqrt2, -invSqrt2, invSqrt2, invSqrt2);

  canvas->setTextSize(1);
  canvas->unloadFont();
  canvas->pushSprite(0, 0);
  delay(1000);
}

void UI::draw(const UIData &data) {
  // Lazily create the full-screen sprite on first draw because M5.begin()
  // (which sets the real display dimensions) runs *after* the global UI
  // instance is constructed.
  if (canvas->width() == 0 || canvas->height() == 0) {
    canvas->setPsram(true);
    canvas->setColorDepth(24);
    canvas->setSwapBytes(false);
    canvas->createSprite(data.displayWidth, data.displayHeight);
  }

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
  const uint32_t pressureWindowMs = 30000;
  const size_t pressurePointCount = PRESSURE_VALUES_LEN;

  auto drawCompressedGraph = [&](const int16_t *values,
                                 size_t valueCount,
                                 const uint32_t *times,
                                 int16_t maxValue,
                                 uint32_t color) {
    if (valueCount < 2 || !values || !times || maxValue <= 0) {
      return;
    }
    uint32_t lastTime = times[valueCount - 1];
    uint32_t renderWindowMs = std::max(pressureWindowMs, lastTime);

    std::vector<int16_t> bucketMin(pressurePointCount, std::numeric_limits<int16_t>::max());
    std::vector<int16_t> bucketMax(pressurePointCount, std::numeric_limits<int16_t>::min());
    std::vector<uint32_t> bucketMinTime(pressurePointCount, 0);
    std::vector<uint32_t> bucketMaxTime(pressurePointCount, 0);
    std::vector<uint16_t> bucketCounts(pressurePointCount, 0);

    for (size_t i = 0; i < valueCount; i++) {
      uint32_t timeValue = times[i];
      size_t bucketIndex = std::min(
          pressurePointCount - 1,
          static_cast<size_t>((timeValue * pressurePointCount) / renderWindowMs));
      int16_t sampleValue = values[i];
      if (sampleValue < bucketMin[bucketIndex]) {
        bucketMin[bucketIndex] = sampleValue;
        bucketMinTime[bucketIndex] = timeValue;
      }
      if (sampleValue > bucketMax[bucketIndex]) {
        bucketMax[bucketIndex] = sampleValue;
        bucketMaxTime[bucketIndex] = timeValue;
      }
      bucketCounts[bucketIndex]++;
    }

    size_t lastBucket = std::min(
        pressurePointCount - 1,
        static_cast<size_t>((lastTime * pressurePointCount) / renderWindowMs));

    struct GraphPoint {
      uint32_t timeMs;
      int16_t value;
    };
    std::vector<GraphPoint> points;
    points.reserve((lastBucket + 1) * 2);

    for (size_t i = 0; i <= lastBucket; i++) {
      uint32_t bucketTime = (renderWindowMs * i) / (pressurePointCount - 1);
      if (bucketCounts[i] == 0) {
        points.push_back({bucketTime, 0});
        continue;
      }
      if (bucketMinTime[i] <= bucketMaxTime[i]) {
        points.push_back({bucketMinTime[i], bucketMin[i]});
        points.push_back({bucketMaxTime[i], bucketMax[i]});
      } else {
        points.push_back({bucketMaxTime[i], bucketMax[i]});
        points.push_back({bucketMinTime[i], bucketMin[i]});
      }
    }

    if (points.empty()) {
      return;
    }

    int16_t previousX = graphStartX + (points.front().timeMs * graphWidth) / renderWindowMs;
    int16_t previousY = graphStartY + graphHeight - points.front().value * graphHeight / maxValue;

    for (size_t i = 1; i < points.size(); i++) {
      int16_t currentX = graphStartX + (points[i].timeMs * graphWidth) / renderWindowMs;
      int16_t currentY = graphStartY + graphHeight - points[i].value * graphHeight / maxValue;
      canvas->drawLine(previousX, previousY, currentX, currentY, color);
      previousX = currentX;
      previousY = currentY;
    }
  };

  drawCompressedGraph(data.pressureHistory,
                      data.pressureHistoryCount,
                      data.pressureHistoryTimes,
                      maxPressure,
                      graphColor);

  const int16_t maxWeightValue = 500;
  drawCompressedGraph(data.weightHistory,
                      data.weightHistoryCount,
                      data.pressureHistoryTimes,
                      maxWeightValue,
                      THEME_GRAPH_WARNING);

  

  // Pressure audio bar
  int16_t audioBarHeightCurrent = std::min(
      static_cast<int16_t>((data.lastPressure) * audioBarHeight / (maxPressure)),
      audioBarHeight);
  canvas->fillRect(audioBarX, audioBarY, audioBarWidth, audioBarHeight + 1, THEME_BAR_BG);
  for (int16_t y = 0; y < audioBarHeightCurrent; y++) {
    int16_t pressureAtY = (y * (maxPressure) / audioBarHeight);
    uint32_t lineColor = (pressureAtY <= 6000) ? canvas->color888((pressureAtY * (96 - 64) / 6000) + 64, (pressureAtY * (96 - 64) / 6000) + 64, (pressureAtY * (96 - 64) / 6000) + 64) :
                         (pressureAtY <= 7000) ? canvas->color888(96 - ((pressureAtY - 6000) * 96 / 1000), 96 + ((pressureAtY - 6000) * (255 - 96) / 1000), 96 - ((pressureAtY - 6000) * 96 / 1000)) :
                         (pressureAtY <= 8000) ? canvas->color888(0, 255, 0) :
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

  canvas->fillRect(110, 0, 100, 80, THEME_PANEL_OUTER_BG);
  canvas->fillRect(112, 18, 96, 60, THEME_PANEL_INNER_BG);
  canvas->setTextColor(THEME_PANEL_HEADER_TEXT, THEME_PANEL_OUTER_BG);
  canvas->drawString("WEIGHT G", 112, 2);

  canvas->setTextColor(THEME_PANEL_TEXT, THEME_PANEL_INNER_BG);

  canvas->loadFont(MoonGloss_16);
  String weightStr = String(data.shotWeight, 1) + "g";
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

  canvas->loadFont(MoonGloss_16);
  canvas->setTextColor(THEME_LIGHTACCENTTEXT, THEME_LIGHTACCENTBG);
  String btStatus = data.isBluetoothOn ? "BT ON" : "BT OFF";
  canvas->drawString(btStatus, 4, data.displayHeight - 18);
  String scaleStatus = data.scaleConnected
                           ? String("Scale: ") + String(data.scaleName.c_str())
                           : String("Scale: --");
  canvas->drawRightString(scaleStatus, data.displayWidth - 4, data.displayHeight - 18);

  if (data.debugMode) {
    canvas->setTextColor(THEME_LIGHTTEXT, THEME_DARKBG);
    canvas->setCursor(10, graphStartY + 10);
    canvas->printf("Scale: %s", data.scaleConnected ? data.scaleName.c_str() : "none");
    canvas->setCursor(10, graphStartY + 30);
    canvas->printf("Nearby: %s", data.nearbyScales.c_str());
  }

  canvas->pushSprite(0, 0);
  Serial.println("Draw: Pushed sprite");
}

bool UI::captureScreenshot(const char *filename) {
  if (!canvas || canvas->width() == 0 || canvas->height() == 0) {
    Serial.println("Screenshot: Canvas not initialized");
    return false;
  }

  File file = SPIFFS.open(filename, "w");
  if (!file) {
    Serial.printf("Screenshot: Failed to open %s\n", filename);
    return false;
  }

  int16_t width = canvas->width();
  int16_t height = canvas->height();
  uint8_t bitsPerPixel = 24;
  uint32_t rowSize = ((width * bitsPerPixel + 31) / 32) * 4;
  uint32_t imageSize = rowSize * height;
  uint32_t fileSize = 54 + imageSize;
  uint32_t dataOffset = 54;

  uint8_t header[54] = {
    'B', 'M',
    (uint8_t)(fileSize), (uint8_t)(fileSize >> 8), (uint8_t)(fileSize >> 16), (uint8_t)(fileSize >> 24),
    0, 0, 0, 0,
    (uint8_t)(dataOffset), (uint8_t)(dataOffset >> 8), (uint8_t)(dataOffset >> 16), (uint8_t)(dataOffset >> 24),
    40, 0, 0, 0,
    (uint8_t)(width), (uint8_t)(width >> 8), (uint8_t)(width >> 16), (uint8_t)(width >> 24),
    (uint8_t)(height), (uint8_t)(height >> 8), (uint8_t)(height >> 16), (uint8_t)(height >> 24),
    1, 0,
    (uint8_t)(bitsPerPixel), 0,
    0, 0, 0, 0,
    (uint8_t)(imageSize), (uint8_t)(imageSize >> 8), (uint8_t)(imageSize >> 16), (uint8_t)(imageSize >> 24),
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0
  };

  file.write(header, 54);

  uint8_t *rowBuffer = (uint8_t *)malloc(rowSize);
  if (!rowBuffer) {
    Serial.println("Screenshot: Failed to allocate row buffer");
    file.close();
    return false;
  }

  for (int16_t y = height - 1; y >= 0; y--) {
    for (int16_t x = 0; x < width; x++) {
      uint32_t color = canvas->readPixel(x, y);
      uint8_t r = (color >> 16) & 0xFF;
      uint8_t g = (color >> 8) & 0xFF;
      uint8_t b = color & 0xFF;
      rowBuffer[x * 3] = b;
      rowBuffer[x * 3 + 1] = g;
      rowBuffer[x * 3 + 2] = r;
    }
    file.write(rowBuffer, rowSize);
  }

  free(rowBuffer);
  file.close();

  Serial.printf("Screenshot: Saved to %s (%dx%d)\n", filename, width, height);
  return true;
}

void UI::captureScreenshotToSerial() {
  if (!canvas || canvas->width() == 0 || canvas->height() == 0) {
    Serial.println("SCREENSHOT_ERROR: Canvas not initialized");
    return;
  }

  int16_t width = canvas->width();
  int16_t height = canvas->height();

  Serial.printf("SCREENSHOT_START:%d:%d\n", width, height);

  for (int16_t y = 0; y < height; y++) {
    for (int16_t x = 0; x < width; x++) {
      uint32_t color = canvas->readPixel(x, y);
      uint8_t r = (color >> 16) & 0xFF;
      uint8_t g = (color >> 8) & 0xFF;
      uint8_t b = color & 0xFF;
      Serial.write(r);
      Serial.write(g);
      Serial.write(b);
    }
  }

  Serial.println("SCREENSHOT_END");
  Serial.printf("Screenshot: Sent %dx%d pixels over serial\n", width, height);
}
