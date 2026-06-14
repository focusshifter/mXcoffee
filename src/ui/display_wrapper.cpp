#include "display_wrapper.h"

DisplayWrapper::DisplayWrapper(M5GFX disp) : display(disp) {}

void DisplayWrapper::drawString(String str, int32_t x, int32_t y) {
  display.drawString(str, x, y);
}

void DisplayWrapper::drawCenterString(String str, int32_t x, int32_t y) {
  display.drawCenterString(str, x, y);
}

int32_t DisplayWrapper::width() { return display.width(); }

int32_t DisplayWrapper::height() { return display.height(); }