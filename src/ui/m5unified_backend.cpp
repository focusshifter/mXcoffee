#include "m5unified_backend.h"
#include <M5Unified.h>

// Constructor
M5UnifiedCanvas::M5UnifiedCanvas(M5GFX *disp) : display(disp) {
  canvas = new M5Canvas(display);
}

// Destructor
M5UnifiedCanvas::~M5UnifiedCanvas() { delete canvas; }

// Check if canvas is valid
bool M5UnifiedCanvas::isValid() const { return canvas != nullptr; }

// Initialize canvas with display
void M5UnifiedCanvas::initCanvas() {
  if (canvas && display) {
    canvas->setColorDepth(16); // Ensure 16-bit color (RGB565)
    canvas->setPsram(true);    // Use PSRAM if available (M5Stack Core2 has it)
  }
}

// Method definitions for M5UnifiedCanvas
void M5UnifiedCanvas::createSprite(int16_t w, int16_t h) {
  canvas->createSprite(w, h);
}

int16_t M5UnifiedCanvas::width() { return canvas->width(); }

int16_t M5UnifiedCanvas::height() { return canvas->height(); }

void M5UnifiedCanvas::fillSprite(uint16_t color) {
  if (!canvas->width() || !canvas->height()) {
    Serial.println("Error: Sprite not initialized before fillSprite");
    return;
  }
  canvas->fillSprite(color);
}

void M5UnifiedCanvas::drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                               uint16_t color) {
  canvas->drawLine(x1, y1, x2, y2, color);
}

void M5UnifiedCanvas::drawRect(int16_t x, int16_t y, int16_t w, int16_t h,
                               uint16_t color) {
  canvas->drawRect(x, y, w, h, color);
}

void M5UnifiedCanvas::fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                               uint16_t color) {
  canvas->fillRect(x, y, w, h, color);
}

void M5UnifiedCanvas::drawFastHLine(int16_t x, int16_t y, int16_t w,
                                    uint16_t color) {
  canvas->drawFastHLine(x, y, w, color);
}

void M5UnifiedCanvas::drawString(const char *str, int16_t x, int16_t y) {
  canvas->drawString(str, x, y);
}

void M5UnifiedCanvas::drawRightString(const char *str, int16_t x, int16_t y) {
  canvas->drawRightString(str, x, y);
}

void M5UnifiedCanvas::drawCenterString(const char *str, int16_t x, int16_t y) {
  canvas->drawCenterString(str, x, y);
}

void M5UnifiedCanvas::setFont(const void *font) {
  canvas->setFont(reinterpret_cast<const lgfx::IFont *>(font));
}

void M5UnifiedCanvas::setTextColor(uint16_t fg, uint16_t bg) {
  canvas->setTextColor(fg, bg);
}

uint16_t M5UnifiedCanvas::color565(uint8_t r, uint8_t g, uint8_t b) {
  return canvas->color565(r, g, b);
}

void M5UnifiedCanvas::pushSprite(int16_t x, int16_t y) {
  canvas->pushSprite(x, y);
}

// Method definitions for M5UnifiedSystem
int16_t M5UnifiedSystem::displayWidth() { return M5.Display.width(); }

int16_t M5UnifiedSystem::displayHeight() { return M5.Display.height(); }

int32_t M5UnifiedSystem::getBatteryLevel() {
  return M5.Power.getBatteryLevel();
}