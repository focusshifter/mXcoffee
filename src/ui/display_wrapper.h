#ifndef DISPLAY_WRAPPER_H
#define DISPLAY_WRAPPER_H

#include <M5GFX.h>

class DisplayWrapper {
private:
  M5GFX display;

public:
  DisplayWrapper(M5GFX disp);
  void drawString(String str, int32_t x, int32_t y);
  void drawCenterString(String str, int32_t x, int32_t y);
  int32_t width();
  int32_t height();
};

extern DisplayWrapper *displayWrapper; // Declaration only

#endif