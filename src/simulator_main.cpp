#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "ui/ui.h"
#include "constants.h"
#include "pressure_sensor/pressure_sensor.h"
#include "SimulatorArduino.h"
#include "ui/simulator_backend.h"

#ifdef SIMULATOR
// Debug print of GFXfont fields
void printFontDebug(const GFXfont* font) {
    printf("Font Debug: bitmap=%p, glyph=%p, first=%d, last=%d, yAdvance=%d\n",
           font->bitmap, font->glyph, font->first, font->last, font->yAdvance);
    // Raw memory dump of the font structure
    const uint8_t* raw = (const uint8_t*)font;
    printf("Raw memory of font struct: ");
    for (int i = 0; i < 24; i++) {
        printf("%02X ", raw[i]);
    }
    printf("\n");
    // Dump the first 10 bytes of the bitmap
    printf("First 10 bytes of bitmap: ");
    for (int i = 0; i < 10; i++) {
        printf("%02X ", font->bitmap[i]);
    }
    printf("\n");
    // Dump the 9 bytes for 'P' at bitmapOffset 289
    printf("Bitmap data for 'P' (offset 289): ");
    for (int i = 289; i < 289 + 9; i++) {
        printf("%02X ", font->bitmap[i]);
    }
    printf("\n");
    // Dump the glyph entry for 'P' (index 48)
    const GFXglyph* glyph = &font->glyph[48];
    printf("Glyph for 'P': bitmapOffset=%d, width=%d, height=%d, xAdvance=%d, xOffset=%d, yOffset=%d\n",
           glyph->bitmapOffset, glyph->width, glyph->height, glyph->xAdvance, glyph->xOffset, glyph->yOffset);
}

// Force inclusion of font data
void forceFontInclusion() {
    volatile const uint8_t* bitmap = DejaVu12Bitmaps;
    volatile const GFXglyph* glyph = DejaVu12Glyphs;
    (void)bitmap;  // Prevent optimization
    (void)glyph;
}
#endif

SimulatorCanvas simCanvas;
CanvasWrapper* canvas = &simCanvas;
UI ui(canvas);
PressureSensor* pressureSensor = nullptr;
int16_t pressureValues[PRESSURE_VALUES_LEN];
DeviceState deviceState = {
    .isAsleep = false,
    .isBluetoothOn = false,
    .deviceConnected = false,
    .lastBTSendSuccessful = false,
    .debugMode = true,
    .lastRefreshTime = 0,
    .lastActivityTime = 0,
    .lastPressure = -1,
    .timerStartTime = 0,
    .shotTotalTime = 0,
    .isTimerRunning = false,
    .pServer = nullptr
};

void setup() {
    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();
    pressureSensor = new PressureSensor(nullptr);
    for (int i = 0; i < PRESSURE_VALUES_LEN; i++) pressureValues[i] = 0;
    printFontDebug(&DejaVu12);
    printFontDebug(&DejaVu24);
    printFontDebug(&DejaVu56);
    forceFontInclusion();  // Ensure font data is linked
}

int16_t getPressure() {
    int16_t pressure = pressureSensor->getPressure();
    for (int i = 0; i < PRESSURE_VALUES_LEN - 1; i++) {
        pressureValues[i] = pressureValues[i + 1];
    }
    pressureValues[PRESSURE_VALUES_LEN - 1] = pressure;
    return pressure;
}

void updateShotTotalTime() {
    if (deviceState.isTimerRunning) {
        unsigned long currentTime = millis();
        deviceState.shotTotalTime += currentTime - deviceState.timerStartTime;
        deviceState.timerStartTime = currentTime;
    }
}

void setTimer(int16_t pressure) {
    unsigned long currentTime = millis();
    if (pressure > 1000) {
        if (!deviceState.isTimerRunning) {
            deviceState.isTimerRunning = true;
            deviceState.timerStartTime = currentTime;
        } else {
            updateShotTotalTime();
        }
    } else {
        if (deviceState.isTimerRunning) {
            updateShotTotalTime();
            deviceState.isTimerRunning = false;
        }
    }
}

int main() {
    setup();
    while (true) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                SDL_Quit();
                TTF_Quit();
                delete pressureSensor;
                return 0;
            }
        }
        if (deviceState.lastRefreshTime + 20 < millis()) {
            deviceState.lastRefreshTime = millis();
            int16_t currentPressure = getPressure();
            if (deviceState.lastPressure == -1 || currentPressure != deviceState.lastPressure) {
                deviceState.lastActivityTime = millis();
                deviceState.lastPressure = currentPressure;
            }
            setTimer(currentPressure);

            UIData data = {
                .pressureValues = {0},
                .lastPressure = currentPressure,
                .hexData = pressureSensor->getHexData(),
                .isBluetoothOn = deviceState.isBluetoothOn,
                .lastBTSendSuccessful = deviceState.lastBTSendSuccessful,
                .batteryLevel = 75,
                .debugMode = deviceState.debugMode,
                .shotTotalTime = deviceState.shotTotalTime,
                .displayWidth = 320,
                .displayHeight = 240,
                .maxPressure = pressureSensor->getMaxPressure(),
                .deviceConnected = deviceState.deviceConnected
            };
            std::copy(pressureValues, pressureValues + PRESSURE_VALUES_LEN, data.pressureValues);
            ui.draw(data);
        }
    }
    return 0;
}