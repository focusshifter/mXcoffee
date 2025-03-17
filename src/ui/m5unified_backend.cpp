#include <M5Unified.h>

#include "canvas_wrapper.h"
#include "system_wrapper.h"

class M5UnifiedCanvas : public CanvasWrapper
{
  private:
  M5Canvas* canvas;
  M5GFX* display;

  public:
  M5UnifiedCanvas(M5GFX* disp) : display(disp)
  {
    canvas = new M5Canvas(display);
  }
  ~M5UnifiedCanvas()
  {
    delete canvas;
  }
  void createSprite(int16_t w, int16_t h) override
  {
    canvas->createSprite(w, h);
  }
  int16_t width() override
  {
    return canvas->width();
  }
  int16_t height() override
  {
    return canvas->height();
  }
  void fillSprite(uint16_t color) override
  {
    canvas->fillSprite(color);
  }
  void drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) override
  {
    canvas->drawLine(x1, y1, x2, y2, color);
  }
  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override
  {
    canvas->drawRect(x, y, w, h, color);
  }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override
  {
    canvas->fillRect(x, y, w, h, color);
  }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override
  {
    canvas->drawFastHLine(x, y, w, color);
  }
  void drawString(const char* str, int16_t x, int16_t y) override
  {
    canvas->drawString(str, x, y);
  }
  void drawRightString(const char* str, int16_t x, int16_t y) override
  {
    canvas->drawRightString(str, x, y);
  }
  void drawCenterString(const char* str, int16_t x, int16_t y) override
  {
    canvas->drawCenterString(str, x, y);
  }
  void setFont(const void* font) override
  {
    canvas->setFont(reinterpret_cast<const lgfx::IFont*>(font));
  }
  void setTextColor(uint16_t fg, uint16_t bg) override
  {
    canvas->setTextColor(fg, bg);
  }
  uint16_t color565(uint8_t r, uint8_t g, uint8_t b) override
  {
    return canvas->color565(r, g, b);
  }
  void pushSprite(int16_t x, int16_t y) override
  {
    canvas->pushSprite(x, y);
  }
};

class M5UnifiedSystem : public SystemWrapper
{
  public:
  int16_t displayWidth() override
  {
    return M5.Display.width();
  }
  int16_t displayHeight() override
  {
    return M5.Display.height();
  }
  int32_t getBatteryLevel() override
  {
    return M5.Power.getBatteryLevel();
  }
};