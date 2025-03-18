#ifdef SIMULATOR

#include "simulator_backend.h"
#include "../constants.h"

SimulatorCanvas::SimulatorCanvas()
    : w(0), h(0), buffer(nullptr), textFg(0xFFFF), textBg(0) {
  SDL_Init(SDL_INIT_VIDEO);
  TTF_Init();
  window = SDL_CreateWindow("M5Stack Simulator", SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, 320, 240, 0);
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                              SDL_TEXTUREACCESS_STREAMING, 320, 240);
  font = TTF_OpenFont("DejaVuSans.ttf", 12);
  if (!font)
    font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 12);
  if (!font)
    SDL_Log("Failed to load font: %s", TTF_GetError());
}

SimulatorCanvas::~SimulatorCanvas() {
  if (buffer)
    delete[] buffer;
  if (font)
    TTF_CloseFont(font);
  TTF_Quit();
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

void SimulatorCanvas::createSprite(int16_t w, int16_t h) {
  this->w = w;
  this->h = h;
  if (buffer)
    delete[] buffer;
  buffer = new uint16_t[w * h];
}

int16_t SimulatorCanvas::width() { return w; }
int16_t SimulatorCanvas::height() { return h; }

void SimulatorCanvas::fillSprite(uint16_t color) {
  for (int i = 0; i < w * h; i++)
    buffer[i] = color;
}

void SimulatorCanvas::drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                               uint16_t color) {
  int dx = abs(x2 - x1), dy = abs(y2 - y1);
  int sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1;
  int err = dx - dy;
  while (true) {
    if (x1 >= 0 && x1 < w && y1 >= 0 && y1 < h)
      buffer[y1 * w + x1] = color;
    if (x1 == x2 && y1 == y2)
      break;
    int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x1 += sx;
    }
    if (e2 < dx) {
      err += dx;
      y1 += sy;
    }
  }
}

void SimulatorCanvas::drawRect(int16_t x, int16_t y, int16_t w, int16_t h,
                               uint16_t color) {
  drawLine(x, y, x + w - 1, y, color);
  drawLine(x, y + h - 1, x + w - 1, y + h - 1, color);
  drawLine(x, y, x, y + h - 1, color);
  drawLine(x + w - 1, y, x + w - 1, y + h - 1, color);
}

void SimulatorCanvas::fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                               uint16_t color) {
  for (int j = y; j < y + h && j < this->h; j++)
    for (int i = x; i < x + w && i < this->w; i++)
      if (i >= 0 && j >= 0)
        buffer[j * this->w + i] = color;
}

void SimulatorCanvas::drawFastHLine(int16_t x, int16_t y, int16_t w,
                                    uint16_t color) {
  for (int i = x; i < x + w && i < this->w; i++)
    if (i >= 0 && y >= 0 && y < this->h)
      buffer[y * this->w + i] = color;
}

void SimulatorCanvas::drawString(const char *str, int16_t x, int16_t y) {
  if (!font) {
    fillRect(x, y, strlen(str) * 8, 12, textFg);
    return;
  }
  SDL_Surface *surface =
      TTF_RenderText_Solid(font, str,
                           {static_cast<Uint8>((textFg >> 11) * 8),
                            static_cast<Uint8>(((textFg >> 5) & 63) * 4),
                            static_cast<Uint8>((textFg & 31) * 8), 255});
  SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surface);
  int tw, th;
  SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
  for (int j = 0; j < th && y + j < h; j++)
    for (int i = 0; i < tw && x + i < w; i++) {
      uint32_t pixel;
      memcpy(&pixel, (uint8_t *)surface->pixels + j * surface->pitch + i * 4,
             4);
      if (pixel & 0xFF000000)
        buffer[(y + j) * w + (x + i)] = textFg;
    }
  SDL_FreeSurface(surface);
  SDL_DestroyTexture(tex);
}

void SimulatorCanvas::drawRightString(const char *str, int16_t x, int16_t y) {
  if (!font) {
    int len = strlen(str) * 8;
    drawString(str, x - len, y);
    return;
  }
  SDL_Surface *surface =
      TTF_RenderText_Solid(font, str,
                           {static_cast<Uint8>((textFg >> 11) * 8),
                            static_cast<Uint8>(((textFg >> 5) & 63) * 4),
                            static_cast<Uint8>((textFg & 31) * 8), 255});
  int tw, th;
  SDL_QueryTexture(SDL_CreateTextureFromSurface(renderer, surface), NULL, NULL,
                   &tw, &th);
  drawString(str, x - tw, y);
  SDL_FreeSurface(surface);
}

void SimulatorCanvas::drawCenterString(const char *str, int16_t x, int16_t y) {
  if (!font) {
    int len = strlen(str) * 8;
    drawString(str, x - len / 2, y);
    return;
  }
  SDL_Surface *surface =
      TTF_RenderText_Solid(font, str,
                           {static_cast<Uint8>((textFg >> 11) * 8),
                            static_cast<Uint8>(((textFg >> 5) & 63) * 4),
                            static_cast<Uint8>((textFg & 31) * 8), 255});
  int tw, th;
  SDL_QueryTexture(SDL_CreateTextureFromSurface(renderer, surface), NULL, NULL,
                   &tw, &th);
  drawString(str, x - tw / 2, y);
  SDL_FreeSurface(surface);
}

void SimulatorCanvas::setFont(const void *font) {
  if (!this->font)
    return;
  if (font == &lgfx::fonts::Font2)
    TTF_SetFontSize(this->font, 12);
  else if (font == &lgfx::fonts::Font4)
    TTF_SetFontSize(this->font, 24);
  else if (font == &lgfx::fonts::Font8)
    TTF_SetFontSize(this->font, 48);
}

void SimulatorCanvas::setTextColor(uint16_t fg, uint16_t bg) {
  textFg = fg;
  textBg = bg;
}

uint16_t SimulatorCanvas::color565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void SimulatorCanvas::pushSprite(int16_t x, int16_t y) {
  SDL_UpdateTexture(texture, NULL, buffer, w * 2);
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, NULL, NULL);
  SDL_RenderPresent(renderer);
}

#endif // SIMULATOR