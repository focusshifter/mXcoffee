#include "pressure_sensor.h"
#include <Arduino.h>

#include <M5GFX.h>
#include <M5Unified.h>

PressureSensor::PressureSensor(m5::I2C_Class *i2c_wire) {
  wire = i2c_wire;
  pressure = 0;
}

int16_t PressureSensor::getPressure() {
  float fpressureValues[3];

  for (int i = 0; i < 3; i++) {
    fpressureValues[i] = getRealPressure();
    delay(5); // Use delay for consistency across targets
  }


  // Serial.print("fpressureValues: ");
  // Serial.print(fpressureValues[0]);
  // Serial.print(", ");
  // Serial.print(fpressureValues[1]);
  // Serial.print(", ");
  // Serial.println(fpressureValues[2]);

  float fpressure =
      (fpressureValues[0] + fpressureValues[1] + fpressureValues[2]) / 3;

  // Serial.print("fpressure avg: ");
  // Serial.println(fpressure);

  pressure = int16_t(fpressure * 1000); // Convert to mbar

  // Serial.print("pressureconv: ");
  // Serial.println(pressure);

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
    // Serial.println("Data read successfully:");
    // Serial.print(" Byte 1: ");
    // Serial.print(data[0], BIN);
    // Serial.print(" Byte 2: ");
    // Serial.print(data[1], BIN);
    // Serial.print(" Byte 3: ");
    // Serial.println(data[2], BIN);

    // Serial.print(" Byte 1: ");
    // Serial.print(data[0], HEX);
    // Serial.print(" Byte 2: ");
    // Serial.print(data[1], HEX);
    // Serial.print(" Byte 3: ");
    // Serial.println(data[2], HEX);

    hex_data = std::to_string(data[0]) + " " + std::to_string(data[1]) + " " +
               std::to_string(data[2]);
  } else {
    // Serial.println("Failed to read data");
    hex_data = "ER ER ER";
  }

  dat = (data[0] << 16) | (data[1] << 8) | data[2];

  // Serial.print("dat: ");
  // Serial.println(dat, BIN);
  // Serial.print("dat: ");
  // Serial.println(dat);

  if (dat & 0x800000) {
    fadc = dat - 16777216.0;
  } else {
    fadc = dat;
  }

  Serial.print("fadc: ");
  Serial.println(fadc);

  float a = 3.9628e-6;
  float b = -4.9509;
  fpressure = a * fadc + b;

  Serial.print("fpressure: ");
  Serial.println(fpressure);
#

  return fpressure;
}

int16_t PressureSensor::getMaxPressure() { return 20000; }
