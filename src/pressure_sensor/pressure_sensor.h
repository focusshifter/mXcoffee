#pragma once

#include <Arduino.h>
#include <M5Unified.h>
#include <M5GFX.h>
#include <string>

class PressureSensor {
  public:
    PressureSensor(m5::I2C_Class * i2c_wire);
    int16_t getPressure();
    int16_t getMaxPressure();
    String getHexData();
  private:
    float getRealPressure();
    String hex_data;
    m5::I2C_Class * wire;
    int16_t pressure;
};
