#ifdef SIMULATOR

#include "simulator_backend.h"
#include "SimulatorArduino.h"

SimulatorCanvas::SimulatorCanvas() {
    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();
    window = SDL_CreateWindow("Simulator", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 320, 240, 0);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 240);
    buffer = new uint32_t[320 * 240];
    memset(buffer, 0, 320 * 240 * sizeof(uint32_t));
    w = 320;
    h = 240;
    textFg = 0xFFFF; // White
    textBg = 0x0000; // Black
    currentFont = nullptr;
    
    // Initialize for OpenFontRender compatibility
    fontSize = 12;
    ttfFont = nullptr;
    
    // Try to load a default font for TTF rendering
    // FIXME
    const char* defaultFontPath = "include/fonts/Dosis-Medium.ttf";
    ttfFont = TTF_OpenFont(defaultFontPath, fontSize);
    if (!ttfFont) {
        printf("WARNING: Could not load font %s: %s\n", defaultFontPath, TTF_GetError());
    } else {
        printf("Loaded TTF font: %s\n", defaultFontPath);
    }
}

SimulatorCanvas::~SimulatorCanvas() {
    delete[] buffer;
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    
    // Clean up TTF resources
    if (ttfFont) {
        TTF_CloseFont(ttfFont);
        ttfFont = nullptr;
    }
    
    TTF_Quit();
    SDL_Quit();
}

void SimulatorCanvas::setPixel(int32_t x, int32_t y, uint16_t color) {
    if (x < 0 || x >= w || y < 0 || y >= h) return;
    uint8_t r = ((color >> 11) & 0x1F) << 3;  // 5-bit R to 8-bit
    uint8_t g = ((color >> 5) & 0x3F) << 2;   // 6-bit G to 8-bit
    uint8_t b = (color & 0x1F) << 3;          // 5-bit B to 8-bit
    buffer[y * w + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;  // ARGB8888, fully opaque
}

void SimulatorCanvas::createSprite(int16_t width, int16_t height) {
    w = width;
    h = height;
    delete[] buffer;
    buffer = new uint32_t[w * h];
    memset(buffer, 0, w * h * sizeof(uint32_t));
    SDL_DestroyTexture(texture);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
}

int16_t SimulatorCanvas::width() { return w; }

int16_t SimulatorCanvas::height() { return h; }

void SimulatorCanvas::fillSprite(uint16_t color) {
    uint8_t r = ((color >> 11) & 0x1F) << 3;
    uint8_t g = ((color >> 5) & 0x3F) << 2;
    uint8_t b = (color & 0x1F) << 3;
    uint32_t argb = (0xFF << 24) | (r << 16) | (g << 8) | b;
    for (int i = 0; i < w * h; i++) buffer[i] = argb;
}

void SimulatorCanvas::drawString(const char *str, int16_t x, int16_t y) {
    // If we have a TTF font, use SDL_TTF for better text rendering
    if (ttfFont && str) {
        // Convert RGB565 to RGB for SDL
        uint8_t r = ((textFg >> 11) & 0x1F) << 3;
        uint8_t g = ((textFg >> 5) & 0x3F) << 2;
        uint8_t b = (textFg & 0x1F) << 3;
        
        SDL_Color color = {r, g, b, 255};
        SDL_Surface* textSurface = TTF_RenderText_Solid(ttfFont, str, color);
        if (textSurface) {
            SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
            if (textTexture) {
                SDL_Rect dstRect = {x, y, textSurface->w, textSurface->h};
                SDL_UpdateTexture(texture, NULL, buffer, w * sizeof(uint32_t));
                SDL_RenderCopy(renderer, texture, NULL, NULL);
                SDL_RenderCopy(renderer, textTexture, NULL, &dstRect);
                SDL_RenderPresent(renderer);
                SDL_DestroyTexture(textTexture);
            }
            SDL_FreeSurface(textSurface);
        }
        return;
    }
    
    // Fall back to bitmap font rendering if TTF is not available
    if (!currentFont || !str) return;
    int16_t cursorX = x;
    Serial_printf("Rendering string '%s' at (%d, %d) with font %p, first: %d, last: %d, yAdvance: %d\n",
                  str, x, y, currentFont, currentFont->first, currentFont->last, currentFont->yAdvance);

    while (*str) {
        uint8_t c = (uint8_t)*str++;
        if (c < currentFont->first || c > currentFont->last) {
            Serial_printf("Skipping char '%c' (out of range: %d < %d or %d > %d)\n",
                          c, c, currentFont->first, c, currentFont->last);
            continue;
        }

        GFXglyph *glyph = &currentFont->glyph[c - currentFont->first];
        uint8_t *bitmap = currentFont->bitmap + glyph->bitmapOffset;

        Serial_printf("Char '%c' at index %d, bitmapOffset: %d, width: %d, height: %d, xAdvance: %d, xOffset: %d, yOffset: %d\n",
                      c, c - currentFont->first, glyph->bitmapOffset, glyph->width, glyph->height, glyph->xAdvance, glyph->xOffset, glyph->yOffset);

        int16_t bitmapX = cursorX + glyph->xOffset;
        int16_t bitmapY = y + glyph->yOffset;

        for (int16_t gy = 0; gy < glyph->height; gy++) {
            for (int16_t gx = 0; gx < glyph->width; gx++) {
                uint8_t byte = bitmap[(gy * ((glyph->width + 7) / 8)) + (gx / 8)];
                uint8_t bit = (byte >> (7 - (gx % 8))) & 1;
                if (bit) {
                    setPixel(bitmapX + gx, bitmapY + gy, textFg);
                } else {
                    setPixel(bitmapX + gx, bitmapY + gy, textBg);  // Explicit background
                }
            }
        }
        cursorX += glyph->xAdvance;
    }
}

void SimulatorCanvas::drawRightString(const char *str, int16_t x, int16_t y) {
    if (!currentFont || !str) return;
    int16_t len = 0;
    const char *p = str;
    while (*p) {
        uint8_t c = (uint8_t)*p++;
        if (c >= currentFont->first && c <= currentFont->last) {
            len += currentFont->glyph[c - currentFont->first].xAdvance;
        }
    }
    drawString(str, x - len, y);
}

void SimulatorCanvas::drawCenterString(const char *str, int16_t x, int16_t y) {
    if (!currentFont || !str) return;
    int16_t len = 0;
    const char *p = str;
    while (*p) {
        uint8_t c = (uint8_t)*p++;
        if (c >= currentFont->first && c <= currentFont->last) {
            len += currentFont->glyph[c - currentFont->first].xAdvance;
        }
    }
    drawString(str, x - len / 2, y);
}

void SimulatorCanvas::setFont(const void* font) {
    currentFont = static_cast<const GFXfont*>(font);
    Serial_printf("Set font to %p\n", currentFont);
    if (currentFont) {
        Serial_printf("Font details: bitmap=%p, glyph=%p, first=%d, last=%d, yAdvance=%d\n",
                      currentFont->bitmap, currentFont->glyph, currentFont->first, currentFont->last, currentFont->yAdvance);
        uint8_t *raw = (uint8_t *)currentFont;
        Serial_print("SetFont Raw memory: ");
        for (int i = 0; i < 24; i++) Serial_printf("%02X ", raw[i]);
        Serial_println("");
    }
}

void SimulatorCanvas::setTextColor(uint16_t fg, uint16_t bg) {
    textFg = fg;
    textBg = bg;
    Serial_printf("Set text color: fg=0x%04X, bg=0x%04X\n", fg, bg);
}

uint16_t SimulatorCanvas::color565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void SimulatorCanvas::pushSprite(int16_t x, int16_t y) {
    SDL_UpdateTexture(texture, NULL, buffer, w * sizeof(uint32_t));
    SDL_RenderClear(renderer);
    SDL_Rect dst = {x, y, w, h};
    SDL_RenderCopy(renderer, texture, NULL, &dst);
    SDL_RenderPresent(renderer);
    Serial_printf("Updated texture with buffer %p, size %dx%d at (%d, %d)\n", buffer, w, h, x, y);
}

void SimulatorCanvas::drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
    int16_t dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int16_t dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int16_t err = dx + dy, e2;

    while (true) {
        setPixel(x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void SimulatorCanvas::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    drawLine(x, y, x + w - 1, y, color);
    drawLine(x, y + h - 1, x + w - 1, y + h - 1, color);
    drawLine(x, y, x, y + h - 1, color);
    drawLine(x + w - 1, y, x + w - 1, y + h - 1, color);
}

void SimulatorCanvas::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    for (int16_t i = x; i < x + w; i++) {
        for (int16_t j = y; j < y + h; j++) {
            setPixel(i, j, color);
        }
    }
}

void SimulatorCanvas::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    for (int16_t i = x; i < x + w; i++) {
        setPixel(i, y, color);
    }
}

const char* SimulatorCanvas::getCurrentFontName() {
    return "Dosis_Medium12pt7b (GFXfont)"; // Static string for simulator
}

void SimulatorCanvas::setFontSize(float size) {
    // Only reload the font if the size has changed
    if (size != fontSize) {
        fontSize = static_cast<int16_t>(size);
        
        // Re-open the font with the new size if we have a valid TTF
        if (ttfFont) {
            TTF_CloseFont(ttfFont);
            ttfFont = nullptr;
        }
        
        const char* defaultFontPath = "include/fonts/Dosis-Medium.ttf";
        ttfFont = TTF_OpenFont(defaultFontPath, fontSize);
        if (!ttfFont) {
            printf("WARNING: Could not load font %s at size %d: %s\n", 
                  defaultFontPath, fontSize, TTF_GetError());
        } else {
            printf("Loaded TTF font: %s at size %d\n", defaultFontPath, fontSize);
        }
    }
}

#endif  // SIMULATOR