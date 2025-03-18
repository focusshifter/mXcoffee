#pragma once

#include <cmath> // For M_PI, sin, round
#include <string>

#ifndef SIMULATOR_OR_DEBUG
#include <Arduino.h>
#include <M5Unified.h>
#endif

class PressureSensor {
public:
#ifdef SIMULATOR_OR_DEBUG
  PressureSensor(void * /* unused */);
#else
  PressureSensor(m5::I2C_Class *i2c_wire);
#endif

#ifdef SIMULATOR_OR_DEBUG
  int16_t getPressure();
  std::string getHexData();
  int16_t getMaxPressure();
#else
  int16_t getPressure();
  std::string getHexData();
  int16_t getMaxPressure();

private:
  float getRealPressure();
#endif

private:
  std::string hex_data;
#ifndef SIMULATOR_OR_DEBUG
  m5::I2C_Class *wire;
#endif
  int16_t pressure;
};