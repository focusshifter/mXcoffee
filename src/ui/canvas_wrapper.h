#ifndef CANVAS_WRAPPER_H
#define CANVAS_WRAPPER_H

#ifdef SIMULATOR
#include "SimulatorArduino.h"
#else
#include <Arduino.h>
#endif

class CanvasWrapper {
public:
    virtual void createSprite(int16_t w, int16_t h) = 0;
    virtual int16_t width() = 0;
    virtual int16_t height() = 0;
    virtual void fillSprite(uint16_t color) = 0;
    virtual void drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                          uint16_t color) = 0;
    virtual void drawRect(int16_t x, int16_t y, int16_t w, int16_t h,
                          uint16_t color) = 0;
    virtual void fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                          uint16_t color) = 0;
    virtual void drawFastHLine(int16_t x, int16_t y, int16_t w,
                               uint16_t color) = 0;
    virtual void drawString(const char *str, int16_t x, int16_t y) = 0;
    virtual void drawRightString(const char *str, int16_t x, int16_t y) = 0;
    virtual void drawCenterString(const char *str, int16_t x, int16_t y) = 0;
    virtual void setFont(const void *font) = 0;
    virtual void setTextColor(uint16_t fg, uint16_t bg) = 0;
    virtual uint16_t color565(uint8_t r, uint8_t g, uint8_t b) = 0;
    virtual void pushSprite(int16_t x, int16_t y) = 0;
    virtual const char* getCurrentFontName() = 0; // Add this method
    
    // Method for OpenFontRender compatibility
    virtual void* getCanvas() { return this; };
};

extern CanvasWrapper *canvas; // Declaration only

#endif