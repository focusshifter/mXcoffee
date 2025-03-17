#pragma once

#include <Arduino.h>
#include <M5GFX.h>
#include <M5Unified.h>

#include <string>

class PressureSensor
{
  public:
  PressureSensor(m5::I2C_Class* i2c_wire);
#ifdef SIMULATOR
  int16_t getPressure()
  {
    return random(0, 12000);
  }  // Mock for simulator
  String getHexData()
  {
    return "SIM DATA";
  }
  int16_t getMaxPressure()
  {
    return 20000;
  }
#else
  int16_t getPressure();
  int16_t getMaxPressure();
  String getHexData();

  private:
  float getRealPressure();
#endif
  private:
  String hex_data;
  m5::I2C_Class* wire;
  int16_t pressure;
};