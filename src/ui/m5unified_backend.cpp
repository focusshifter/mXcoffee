#ifndef SIMULATOR

#include "m5unified_backend.h"
#include <Arduino.h>

M5UnifiedCanvas::M5UnifiedCanvas(M5GFX* display) {
    canvas = new lgfx::LGFX_Sprite(display);
    Serial.println("M5UnifiedCanvas: Constructor called");
    canvas->createSprite(160, 120); // Reduce size to 160x120
    if (canvas->getBuffer() == nullptr) {
        Serial.println("M5UnifiedCanvas: Error - Sprite buffer is null after creation in constructor!");
    } else {
        Serial.println("M5UnifiedCanvas: Sprite created successfully in constructor");
        Serial.print("M5UnifiedCanvas: Sprite width=");
        Serial.print(canvas->width());
        Serial.print(", height=");
        Serial.println(canvas->height());
    }
}

M5UnifiedCanvas::~M5UnifiedCanvas() {
    Serial.println("M5UnifiedCanvas: Destructor called");
    delete canvas;
}

void M5UnifiedCanvas::createSprite(int16_t width, int16_t height) {
    Serial.print("M5UnifiedCanvas: Creating sprite with width=");
    Serial.print(width);
    Serial.print(", height=");
    Serial.println(height);
    canvas->deleteSprite(); // Delete any existing sprite
    canvas->createSprite(width, height);
    if (canvas->getBuffer() == nullptr) {
        Serial.println("M5UnifiedCanvas: Error - Sprite buffer is null after creation!");
    } else {
        Serial.println("M5UnifiedCanvas: Sprite created successfully");
        Serial.print("M5UnifiedCanvas: Sprite width=");
        Serial.print(canvas->width());
        Serial.print(", height=");
        Serial.println(canvas->height());
    }
}

void M5UnifiedCanvas::deleteSprite() {
    Serial.println("M5UnifiedCanvas: Deleting sprite");
    canvas->deleteSprite();
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
    if (canvas->getBuffer() == nullptr) {
        Serial.println("M5UnifiedCanvas: Error - Sprite buffer is null before fillSprite!");
        return;
    }
    canvas->fillSprite(color);
}

void M5UnifiedCanvas::drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
    if (canvas->getBuffer() == nullptr) {
        Serial.println("M5UnifiedCanvas: Error - Sprite buffer is null before drawLine!");
        return;
    }
    canvas->drawLine(x1, y1, x2, y2, color);
}

void M5UnifiedCanvas::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (canvas->getBuffer() == nullptr) {
        Serial.println("M5UnifiedCanvas: Error - Sprite buffer is null before drawRect!");
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
    if (canvas->getBuffer() == nullptr) {
        Serial.println("M5UnifiedCanvas: Error - Sprite buffer is null before fillRect!");
        return;
    }
    canvas->fillRect(x, y, w, h, color);
}

void M5UnifiedCanvas::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    if (canvas->getBuffer() == nullptr) {
        Serial.println("M5UnifiedCanvas: Error - Sprite buffer is null before drawFastHLine!");
        return;
    }
    canvas->drawFastHLine(x, y, w, color);
}

void M5UnifiedCanvas::drawString(const char* str, int16_t x, int16_t y) {
    Serial.print("M5UnifiedCanvas: Drawing string '");
    Serial.print(str);
    Serial.print("' at (");
    Serial.print(x);
    Serial.print(", ");
    Serial.print(y);
    Serial.println(")");
    if (canvas->getBuffer() == nullptr) {
        Serial.println("M5UnifiedCanvas: Error - Sprite buffer is null before drawString!");
        return;
    }
    canvas->drawString(str, x, y);
}

void M5UnifiedCanvas::drawRightString(const char* str, int16_t x, int16_t y) {
    if (canvas->getBuffer() == nullptr) {
        Serial.println("M5UnifiedCanvas: Error - Sprite buffer is null before drawRightString!");
        return;
    }
    canvas->drawRightString(str, x, y);
}

void M5UnifiedCanvas::drawCenterString(const char* str, int16_t x, int16_t y) {
    if (canvas->getBuffer() == nullptr) {
        Serial.println("M5UnifiedCanvas: Error - Sprite buffer is null before drawCenterString!");
        return;
    }
    canvas->drawCenterString(str, x, y);
}

void M5UnifiedCanvas::setFont(const void* font) {
    Serial.print("Setting font: ");
    if (font == &FreeMono12pt7b) {
        Serial.println("FreeMono12pt7b");
    } else {
        Serial.println("Unknown font");
    }
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
    if (canvas->getBuffer() == nullptr) {
        Serial.println("M5UnifiedCanvas: Error - Sprite buffer is null before pushSprite!");
        // Attempt to recreate the sprite
        Serial.println("M5UnifiedCanvas: Attempting to recreate sprite");
        canvas->createSprite(160, 120);
        if (canvas->getBuffer() == nullptr) {
            Serial.println("M5UnifiedCanvas: Error - Sprite recreation failed!");
            return;
        } else {
            Serial.println("M5UnifiedCanvas: Sprite recreated successfully");
        }
    }
    canvas->pushSprite(x, y, 2, 2); // Scale by 2x to match 320x240
    Serial.println("M5UnifiedCanvas: pushSprite completed with 2x scaling");
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
    if (font == reinterpret_cast<const lgfx::IFont*>(&FreeMono12pt7b)) return "FreeMono12pt7b (GFXfont)";
    return "Unknown (possibly TrueType fallback)";
}

#endif  // !SIMULATOR