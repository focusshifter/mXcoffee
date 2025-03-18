#include "pressure_sensor.h"

#ifdef SIMULATOR
#include "SimulatorArduino.h"
#else
#include <Arduino.h>
#endif

#ifdef SIMULATOR_OR_DEBUG
PressureSensor::PressureSensor(void * /* unused */) { pressure = 0; }
#else
#include <M5GFX.h>
#include <M5Unified.h>

PressureSensor::PressureSensor(m5::I2C_Class *i2c_wire) {
  wire = i2c_wire;
  pressure = 0;
}
#endif

#ifndef SIMULATOR_OR_DEBUG
int16_t PressureSensor::getPressure() {
  float fpressureValues[3];

  for (int i = 0; i < 3; i++) {
    fpressureValues[i] = getRealPressure();
    delay(5); // Use delay for consistency across targets
  }

#ifdef SIMULATOR
  Serial_print("fpressureValues: ");
  Serial_print(fpressureValues[0]);
  Serial_print(", ");
  Serial_print(fpressureValues[1]);
  Serial_print(", ");
  Serial_println(fpressureValues[2]);
#else
  Serial.print("fpressureValues: ");
  Serial.print(fpressureValues[0]);
  Serial.print(", ");
  Serial.print(fpressureValues[1]);
  Serial.print(", ");
  Serial.println(fpressureValues[2]);
#endif

  float fpressure =
      (fpressureValues[0] + fpressureValues[1] + fpressureValues[2]) / 3;

#ifdef SIMULATOR
  Serial_print("fpressure avg: ");
  Serial_println(fpressure);
#else
  Serial.print("fpressure avg: ");
  Serial.println(fpressure);
#endif

  pressure = int16_t(fpressure * 1000); // Convert to mbar

#ifdef SIMULATOR
  Serial_print("pressureconv: ");
  Serial_println(pressure);
#else
  Serial.print("pressureconv: ");
  Serial.println(pressure);
#endif

  if (pressure < 0) {
    pressure = 0;
  }

  return pressure;
}

std::string PressureSensor::getHexData() { return hex_data; }

float PressureSensor::getRealPressure() {
  float fadc = 0;
  float fpressure = 0;
  uint8_t data[3];
  uint32_t dat = 0;
  if (wire->readRegister(0x6D, 0x06, data, 3, 100000)) {
#ifdef SIMULATOR
    Serial_println("Data read successfully:");
    Serial_print(" Byte 1: ");
    Serial_print(data[0], BIN);
    Serial_print(" Byte 2: ");
    Serial_print(data[1], BIN);
    Serial_print(" Byte 3: ");
    Serial_println(data[2], BIN);
#else
    Serial.println("Data read successfully:");
    Serial.print(" Byte 1: ");
    Serial.print(data[0], BIN);
    Serial.print(" Byte 2: ");
    Serial.print(data[1], BIN);
    Serial.print(" Byte 3: ");
    Serial.println(data[2], BIN);
#endif

#ifdef SIMULATOR
    Serial_print(" Byte 1: ");
    Serial_print(data[0], HEX);
    Serial_print(" Byte 2: ");
    Serial_print(data[1], HEX);
    Serial_print(" Byte 3: ");
    Serial_println(data[2], HEX);
#else
    Serial.print(" Byte 1: ");
    Serial.print(data[0], HEX);
    Serial.print(" Byte 2: ");
    Serial.print(data[1], HEX);
    Serial.print(" Byte 3: ");
    Serial.println(data[2], HEX);
#endif

    hex_data = std::to_string(data[0]) + " " + std::to_string(data[1]) + " " +
               std::to_string(data[2]);
  } else {
#ifdef SIMULATOR
    Serial_println("Failed to read data");
    hex_data = "ER ER ER";
#else
    Serial.println("Failed to read data");
    hex_data = "ER ER ER";
#endif
  }

  dat = (data[0] << 16) | (data[1] << 8) | data[2];

#ifdef SIMULATOR
  Serial_print("dat: ");
  Serial_println(dat, BIN);
  Serial_print("dat: ");
  Serial_println(dat);
#else
  Serial.print("dat: ");
  Serial.println(dat, BIN);
  Serial.print("dat: ");
  Serial.println(dat);
#endif

  if (dat & 0x800000) {
    fadc = dat - 16777216.0;
  } else {
    fadc = dat;
  }

#ifdef SIMULATOR
  Serial_print("fadc: ");
  Serial_println(fadc);
#else
  Serial.print("fadc: ");
  Serial.println(fadc);
#endif

  float a = 3.9628e-6;
  float b = -4.9509;
  fpressure = a * fadc + b;

#ifdef SIMULATOR
  Serial_print("fpressure: ");
  Serial_println(fpressure);
#else
  Serial.print("fpressure: ");
  Serial.println(fpressure);
#endif

  return fpressure;
}

int16_t PressureSensor::getMaxPressure() { return 20000; }
#endif

#ifdef SIMULATOR_OR_DEBUG
int16_t PressureSensor::getPressure() {
  static uint32_t lastTime = 0;
  static int16_t pressure = 0;
  if (millis() - lastTime > 30) {
    lastTime = millis();
    double normalized_time = 2.0 * M_PI * (millis() / 10000.0);
    double sin_value = sin(normalized_time - M_PI / 2);
    pressure = int16_t(round(0.5 * (sin_value + 1.0) * 12000));
  }
  return pressure;
}

std::string PressureSensor::getHexData() { return "SIM DATA"; }

int16_t PressureSensor::getMaxPressure() { return 20000; }
#endif