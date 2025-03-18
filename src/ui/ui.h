#pragma once

#include "../constants.h"
#include "canvas_wrapper.h"

class UI {
public:
  UI(CanvasWrapper *canvas) : canvas(canvas) {}
  void draw(const UIData &data);

private:
  CanvasWrapper *canvas;
};