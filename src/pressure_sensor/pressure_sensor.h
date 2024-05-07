#pragma once

#include <Arduino.h>
#include <M5Unified.h>
#include <M5GFX.h>
#include <string>

class PressureSensor {
  public:
    PressureSensor();
    void start();
    int16_t getPressure();
    void stop();

    int16_t getMaxPressure();
  private:
    int16_t pressure;
};
