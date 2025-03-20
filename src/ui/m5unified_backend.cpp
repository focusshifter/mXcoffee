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
    Serial.print("M5UnifiedCanvas: width() called, returning ");
    Serial.println(w);
    return w;
}

int16_t M5UnifiedCanvas::height() {
    int16_t h = canvas->height();
    Serial.print("M5UnifiedCanvas: height() called, returning ");
    Serial.println(h);
    return h;
}

void M5UnifiedCanvas::fillSprite(uint16_t color) {
    Serial.print("M5UnifiedCanvas: Filling sprite with color=");
    Serial.println(color, HEX);
    if (!canvas || !canvas->width() || !canvas->height()) {
        Serial.println("M5UnifiedCanvas: Error - Sprite not initialized before fillSprite!");
        return;
    }
    canvas->fillSprite(color);
}
void M5UnifiedCanvas::drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
    if (!canvas->width() || !canvas->height()) {
        Serial.println("M5UnifiedCanvas: Error - Sprite not initialized before drawLine!");
        return;
    }
    canvas->drawLine(x1, y1, x2, y2, color);
}

void M5UnifiedCanvas::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (!canvas->width() || !canvas->height()) {
        Serial.println("M5UnifiedCanvas: Error - Sprite not initialized before drawRect!");
        return;
    }
    canvas->drawRect(x, y, w, h, color);
}

void M5UnifiedCanvas::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    Serial.print("M5UnifiedCanvas: Filling rect at (");
    Serial.print(x);
    Serial.print(", ");
    Serial.print(y);
    Serial.print(") with width=");
    Serial.print(w);
    Serial.print(", height=");
    Serial.print(h);
    Serial.print(", color=");
    Serial.println(color, HEX);
    if (!canvas->width() || !canvas->height()) {
        Serial.println("M5UnifiedCanvas: Error - Sprite not initialized before fillRect!");
        return;
    }
    canvas->fillRect(x, y, w, h, color);
}

void M5UnifiedCanvas::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    if (!canvas->width() || !canvas->height()) {
        Serial.println("M5UnifiedCanvas: Error - Sprite not initialized before drawFastHLine!");
        return;
    }
    canvas->drawFastHLine(x, y, w, color);
}

void M5UnifiedCanvas::drawString(const char *str, int16_t x, int16_t y) {
    Serial.print("M5UnifiedCanvas: Drawing string '");
    Serial.print(str);
    Serial.print("' at (");
    Serial.print(x);
    Serial.print(", ");
    Serial.print(y);
    Serial.println(")");
    if (!canvas->width() || !canvas->height()) {
        Serial.println("M5UnifiedCanvas: Error - Sprite not initialized before drawString!");
        return;
    }
    canvas->drawString(str, x, y);
}

void M5UnifiedCanvas::drawRightString(const char *str, int16_t x, int16_t y) {
    if (!canvas->width() || !canvas->height()) {
        Serial.println("M5UnifiedCanvas: Error - Sprite not initialized before drawRightString!");
        return;
    }
    canvas->drawRightString(str, x, y);
}

void M5UnifiedCanvas::drawCenterString(const char *str, int16_t x, int16_t y) {
    if (!canvas->width() || !canvas->height()) {
        Serial.println("M5UnifiedCanvas: Error - Sprite not initialized before drawCenterString!");
        return;
    }
    canvas->drawCenterString(str, x, y);
}

void M5UnifiedCanvas::setFont(const void *font) {
    canvas->setFont(reinterpret_cast<const lgfx::IFont*>(font));
    Serial.print("Current font after set: ");
    Serial.println(getCurrentFontName());
}

void M5UnifiedCanvas::setTextColor(uint16_t fg, uint16_t bg) {
    Serial.print("M5UnifiedCanvas: Setting text color to fg=");
    Serial.print(fg, HEX);
    Serial.print(", bg=");
    Serial.println(bg, HEX);
    canvas->setTextColor(fg, bg);
}

uint16_t M5UnifiedCanvas::color565(uint8_t r, uint8_t g, uint8_t b) {
    return canvas->color565(r, g, b);
}

void M5UnifiedCanvas::pushSprite(int16_t x, int16_t y) {
    Serial.print("M5UnifiedCanvas: Pushing sprite at x=");
    Serial.print(x);
    Serial.print(", y=");
    Serial.println(y);
    if (!canvas->width() || !canvas->height()) {
        Serial.println("M5UnifiedCanvas: Error - Sprite not initialized before pushSprite!");
        return;
    }
    canvas->pushSprite(x, y);
    Serial.println("M5UnifiedCanvas: pushSprite completed");
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