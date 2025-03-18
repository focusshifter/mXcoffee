#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "SimulatorArduino.h"
#include "constants.h"
#include "pressure_sensor/pressure_sensor.h"
#include "ui/simulator_backend.h"
#include "ui/ui.h"

SimulatorCanvas simCanvas;
CanvasWrapper *canvas = &simCanvas;
UI ui(canvas);
PressureSensor *pressureSensor = nullptr;
int16_t pressureValues[PRESSURE_VALUES_LEN];
DeviceState deviceState = {.isAsleep = false,
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
                           .pServer = nullptr};

namespace lgfx {
namespace fonts {
const void *Font2 = (void *)1;
const void *Font4 = (void *)2;
const void *Font8 = (void *)3;
} // namespace fonts
} // namespace lgfx

void setup() {
  SDL_Init(SDL_INIT_VIDEO);
  TTF_Init();
  pressureSensor = new PressureSensor(nullptr);
  for (int i = 0; i < PRESSURE_VALUES_LEN; i++)
    pressureValues[i] = 0;
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
      if (deviceState.lastPressure == -1 ||
          currentPressure != deviceState.lastPressure) {
        deviceState.lastActivityTime = millis();
        deviceState.lastPressure = currentPressure;
      }
      setTimer(currentPressure);

      UIData data = {.pressureValues = {0},
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
                     .deviceConnected = deviceState.deviceConnected};
      std::copy(pressureValues, pressureValues + PRESSURE_VALUES_LEN,
                data.pressureValues);
      ui.draw(data);
    }
  }
  return 0;
}