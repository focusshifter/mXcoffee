#ifndef SIMULATOR

#include "m5unified_backend.h"
#include <Arduino.h>

M5UnifiedCanvas::M5UnifiedCanvas(M5GFX *disp) : display(disp) {
    canvas = new M5Canvas(display);
    if (!canvas) {
        Serial.println("M5UnifiedCanvas: Failed to allocate M5Canvas!");
        while (1) delay(1000);
    }
    Serial.println("M5UnifiedCanvas: Constructor called");
    canvas->setColorDepth(16);
    canvas->setPsram(true);
}

M5UnifiedCanvas::~M5UnifiedCanvas() {
    Serial.println("M5UnifiedCanvas: Destructor called");
    delete canvas;
}

void M5UnifiedCanvas::createSprite(int16_t w, int16_t h) {
    Serial.print("M5UnifiedCanvas: Creating sprite with width=");
    Serial.print(w);
    Serial.print(", height=");
    Serial.println(h);
    if (!canvas->createSprite(w, h)) {
        Serial.println("M5UnifiedCanvas: Failed to create sprite!");
        while (1) delay(1000);
    }
    Serial.print("M5UnifiedCanvas: Sprite width=");
    Serial.print(canvas->width());
    Serial.print(", height=");
    Serial.println(canvas->height());
}


int16_t M5UnifiedCanvas::width() {
    int16_t w = canvas->width();
    return w;
}

int16_t M5UnifiedCanvas::height() {
    int16_t h = canvas->height();
    return h;
}

void M5UnifiedCanvas::fillSprite(uint32_t color) {
    if (!canvas || !canvas->width() || !canvas->height()) {
        Serial.println("M5UnifiedCanvas: Error - Sprite not initialized before fillSprite!");
        return;
    }
    canvas->fillSprite(color);
}
void M5UnifiedCanvas::drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint32_t color) {
    if (!canvas->width() || !canvas->height()) {
        Serial.println("M5UnifiedCanvas: Error - Sprite not initialized before drawLine!");
        return;
    }
    canvas->drawLine(x1, y1, x2, y2, color);
}

void M5UnifiedCanvas::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color) {
    if (!canvas->width() || !canvas->height()) {
        Serial.println("M5UnifiedCanvas: Error - Sprite not initialized before drawRect!");
        return;
    }
    canvas->drawRect(x, y, w, h, color);
}

void M5UnifiedCanvas::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color) {
    if (!canvas->width() || !canvas->height()) {
        Serial.println("M5UnifiedCanvas: Error - Sprite not initialized before fillRect!");
        return;
    }
    canvas->fillRect(x, y, w, h, color);
}

void M5UnifiedCanvas::drawFastHLine(int16_t x, int16_t y, int16_t w, uint32_t color) {
    if (!canvas->width() || !canvas->height()) {
        Serial.println("M5UnifiedCanvas: Error - Sprite not initialized before drawFastHLine!");
        return;
    }
    canvas->drawFastHLine(x, y, w, color);
}

void M5UnifiedCanvas::drawString(const String& string, int16_t x, int16_t y) {
    if (!canvas->width() || !canvas->height()) {
        Serial.println("M5UnifiedCanvas: Error - Sprite not initialized before drawString!");
        return;
    }
    canvas->drawString(string, x, y);
}

void M5UnifiedCanvas::drawRightString(const String& string, int16_t x, int16_t y) {
    if (!canvas->width() || !canvas->height()) {
        Serial.println("M5UnifiedCanvas: Error - Sprite not initialized before drawRightString!");
        return;
    }
    canvas->drawRightString(string, x, y);
}

void M5UnifiedCanvas::drawCenterString(const String& string, int16_t x, int16_t y) {
    if (!canvas->width() || !canvas->height()) {
        Serial.println("M5UnifiedCanvas: Error - Sprite not initialized before drawCenterString!");
        return;
    }
    canvas->drawCenterString(string, x, y);
}

void M5UnifiedCanvas::setFont(const void *font) {
    canvas->setFont(reinterpret_cast<const lgfx::IFont*>(font));
}

void M5UnifiedCanvas::loadFont(const uint8_t* array) {
    canvas->loadFont(array);
}

void M5UnifiedCanvas::setTextColor(uint32_t fg, uint32_t bg) {
    canvas->setTextColor(fg, bg);
}

uint32_t M5UnifiedCanvas::color888(uint8_t r, uint8_t g, uint8_t b) {
    return canvas->color888(r, g, b);
}

void M5UnifiedCanvas::pushSprite(int16_t x, int16_t y) {
    if (!canvas->width() || !canvas->height()) {
        Serial.println("M5UnifiedCanvas: Error - Sprite not initialized before pushSprite!");
        return;
    }
    canvas->pushSprite(x, y);
}

const char* M5UnifiedCanvas::getCurrentFontName() {
    const lgfx::IFont* font = canvas->getFont();
    if (font == nullptr) {
        return "No font set";
    }
    if (font == &lgfx::fonts::Font0) return "Font0";
    if (font == &lgfx::fonts::Font2) return "Font2";
    if (font == &lgfx::fonts::Font4) return "Font4";
    if (font == &lgfx::fonts::Font6) return "Font6";
    if (font == &lgfx::fonts::Font8) return "Font8";
    return "Unknown (possibly TrueType fallback)";
}

#endif  // !SIMULATOR