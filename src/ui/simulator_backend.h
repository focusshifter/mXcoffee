#ifndef SIMULATOR_BACKEND_H
#define SIMULATOR_BACKEND_H

#ifdef SIMULATOR

#include "canvas_wrapper.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

class SimulatorCanvas : public CanvasWrapper {
private:
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *texture;
  uint16_t *buffer;
  int16_t w, h;
  uint16_t textFg, textBg;
  TTF_Font *font;

public:
  SimulatorCanvas();
  ~SimulatorCanvas();
  void createSprite(int16_t w, int16_t h) override;
  int16_t width() override;
  int16_t height() override;
  void fillSprite(uint16_t color) override;
  void drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                uint16_t color) override;
  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h,
                uint16_t color) override;
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                uint16_t color) override;
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override;
  void drawString(const char *str, int16_t x, int16_t y) override;
  void drawRightString(const char *str, int16_t x, int16_t y) override;
  void drawCenterString(const char *str, int16_t x, int16_t y) override;
  void setFont(const void *font) override;
  void setTextColor(uint16_t fg, uint16_t bg) override;
  uint16_t color565(uint8_t r, uint8_t g, uint8_t b) override;
  void pushSprite(int16_t x, int16_t y) override;
};

#endif // SIMULATOR

#endif // SIMULATOR_BACKEND_H