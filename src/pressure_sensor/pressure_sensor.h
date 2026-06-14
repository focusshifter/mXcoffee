#pragma once

#include <cmath> // For M_PI, sin, round
#include <stdint.h>
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

public:
  void resetSimulation();

private:
  std::string hex_data;
#ifndef SIMULATOR_OR_DEBUG
  m5::I2C_Class *wire;
#endif
#if defined(PRESSURE_SENSOR_SIMULATED) && PRESSURE_SENSOR_SIMULATED
  uint32_t simulatedStartMs = 0;
#endif
  int16_t pressure;
};
