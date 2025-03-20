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
  Serial.println("UI: Starting draw");

  // Fill the sprite with a color to confirm it's being rendered
  canvas->fillSprite(TFT_BLUE); // Use blue to distinguish from black
  Serial.println("Draw: Filled sprite with blue");

  // Draw a white rectangle to confirm drawing operations
  canvas->fillRect(50, 50, 100, 100, TFT_WHITE);
  Serial.println("Draw: Drew white rectangle at (50, 50, 100, 100)");

  // Draw some text to confirm font rendering
  canvas->setFont(&FreeMono12pt7b);
  Serial.print("Draw: Set font: ");
  Serial.println(canvas->getCurrentFontName());
  canvas->setTextColor(TFT_RED, TFT_BLUE);
  canvas->drawString("Test Text", 10, 10);
  Serial.println("Draw: Drew test text at (10, 10)");

  // Push the sprite to the display
  canvas->pushSprite(0, 0);
  Serial.println("Draw: Pushed sprite");
}