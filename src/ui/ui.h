#pragma once

#include "../constants.h"
#include <M5GFX.h>

class UI {
private:
  M5Canvas *canvas;
  M5GFX *display;

public:
  UI(M5GFX *display);
  ~UI();
  void drawSplash();
  void draw(const UIData &data);
};