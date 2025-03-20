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
    currentFont = &DejaVu12; // Default font
}

SimulatorCanvas::~SimulatorCanvas() {
    delete[] buffer;
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
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
    if (!currentFont || !str) return;
    Serial_printf("Rendering string '%s' at (%d, %d) with font %p\n", str, x, y, currentFont);

    const GFXglyph *glyph;
    int16_t cursorX = x;
    for (int i = 0; str[i]; i++) {
        uint8_t c = str[i];
        if (c < currentFont->first || c > currentFont->last) continue;
        glyph = &currentFont->glyph[c - currentFont->first];
        uint8_t *bitmap = currentFont->bitmap + glyph->bitmapOffset;

        Serial_printf("Char '%c' at index %d, bitmapOffset: %d, width: %d, height: %d, xAdvance: %d, xOffset: %d, yOffset: %d\n",
                      c, c - currentFont->first, glyph->bitmapOffset, glyph->width, glyph->height, glyph->xAdvance, glyph->xOffset, glyph->yOffset);

        int bytesPerRow = (glyph->width + 7) / 8;
        Serial_printf("Bitmap data for '%c': ", c);
        for (int j = 0; j < glyph->height * bytesPerRow; j++) {
            Serial_printf("%02X ", bitmap[j]);
        }
        Serial_println("");

        Serial_println("Bitmap visual:");
        for (int row = 0; row < glyph->height; row++) {
            for (int col = 0; col < glyph->width; col++) {
                int byteIndex = row * bytesPerRow + col / 8;
                int bitIndex = 7 - (col % 8);
                bool pixel = (bitmap[byteIndex] >> bitIndex) & 1;
                Serial_print(pixel ? "█" : " ");
            }
            Serial_println("");
        }

        for (int row = 0; row < glyph->height; row++) {
            for (int col = 0; col < glyph->width; col++) {
                int byteIndex = row * bytesPerRow + col / 8;
                int bitIndex = 7 - (col % 8);
                bool pixel = (bitmap[byteIndex] >> bitIndex) & 1;
                if (pixel) {
                    setPixel(cursorX + col + glyph->xOffset, y + row + glyph->yOffset, textFg);
                } else {
                    setPixel(cursorX + col + glyph->xOffset, y + row + glyph->yOffset, textBg);
                }
            }
        }
        cursorX += glyph->xAdvance;
    }
}

void SimulatorCanvas::drawRightString(const char *str, int16_t x, int16_t y) {
    if (!currentFont || !str) return;
    int16_t width = 0;
    for (int i = 0; str[i]; i++) {
        uint8_t c = str[i];
        if (c < currentFont->first || c > currentFont->last) continue;
        const GFXglyph *glyph = &currentFont->glyph[c - currentFont->first];
        width += glyph->xAdvance;
    }
    drawString(str, x - width, y);
}

void SimulatorCanvas::drawCenterString(const char *str, int16_t x, int16_t y) {
    if (!currentFont || !str) return;
    int16_t width = 0;
    for (int i = 0; str[i]; i++) {
        uint8_t c = str[i];
        if (c < currentFont->first || c > currentFont->last) continue;
        const GFXglyph *glyph = &currentFont->glyph[c - currentFont->first];
        width += glyph->xAdvance;
    }
    drawString(str, x - width / 2, y);
}

void SimulatorCanvas::setFont(const void* font) {
    currentFont = static_cast<const GFXfont*>(font);
    Serial_printf("Set font to %p\n", currentFont);
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
    if (currentFont == &DejaVu12) return "DejaVu12 (GFXfont)";
    if (currentFont == &DejaVu24) return "DejaVu24 (GFXfont)";
    if (currentFont == &DejaVu56) return "DejaVu56 (GFXfont)";
    return "Unknown font";
}

#endif  // SIMULATOR