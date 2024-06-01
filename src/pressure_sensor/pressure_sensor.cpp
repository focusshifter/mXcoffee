#include <Arduino.h>
#include <M5Unified.h>
#include "pressure_sensor.h"

// 
// Implementation for WNK80MA pressure sensor I2C
//

PressureSensor::PressureSensor(m5::I2C_Class * i2c_wire) {
    wire = i2c_wire;
    pressure = 0;
}

int16_t PressureSensor::getPressure() {
    float fpressureValues[3];

    for (int i = 0; i < 3; i++) {
        fpressureValues[i] = getRealPressure();
        M5.delay(5);
    }

    Serial.print("fpressureValues: "); Serial.print(fpressureValues[0]); Serial.print(", "); Serial.print(fpressureValues[1]); Serial.print(", "); Serial.println(fpressureValues[2]);

    float fpressure = (fpressureValues[0] + fpressureValues[1] + fpressureValues[2]) / 3;

    Serial.print("fpressure avg: "); Serial.println(fpressure);
 
    // fpressure = 1.238 * fpressure - 1.044;

    // pressure = int16_t(fpressure * 10) - 1000; // Convert to mbar and compensate for atmospheric pressure
    pressure = int16_t(fpressure * 1000); // Convert to mbar

    Serial.print("pressureconv: "); Serial.println(pressure);

    if (pressure < 0) {
        pressure = 0;
    }

    return pressure;
}

String PressureSensor::getHexData() {
    return hex_data;
}

float PressureSensor::getRealPressure() {
    float fadc = 0;
    float fpressure = 0;
    uint8_t data[3];
    uint32_t dat = 0;
    // Read 3 bytes from register 0x06 of the device with address 0x6D
    if (wire->readRegister(0x6D, 0x06, data, 3, 100000)) {
        Serial.println("Data read successfully:");
        Serial.print(" Byte 1: "); Serial.print(data[0], BIN);
        Serial.print(" Byte 2: "); Serial.print(data[1], BIN);
        Serial.print(" Byte 3: "); Serial.print(data[2], BIN);

        Serial.println("");

        Serial.print(" Byte 1: "); Serial.print(data[0], HEX); // Print the first byte
        Serial.print(" Byte 2: "); Serial.print(data[1], HEX); // Print the second byte
        Serial.print(" Byte 3: "); Serial.print(data[2], HEX); // Print the third byte

        Serial.println("");


        hex_data = String(data[0], HEX) + " " + String(data[1], HEX) + " " + String(data[2], HEX);
    } else {
        Serial.println("Failed to read data");
        hex_data = String("ER ER ER");
    }

    dat = (data[0] << 16) | (data[1] << 8) | data[2];

    Serial.print("dat: "); Serial.println(dat, BIN);
    Serial.print("dat: "); Serial.println(dat);

    if (dat & 0x800000) {
        fadc = dat - 16777216.0;
    } else {
        fadc = dat;
    }

    Serial.print("fadc: "); Serial.println(fadc);

    // float vref = 4.0; // 5V
    // float adc = vref * fadc / 8388608.0; // 2^23
    // float lower_range_limit = 0;
    // float upper_range_limit = 2000; // 20 bar or 2000 kPa

    // float range = upper_range_limit - lower_range_limit;

    // float p_voltage_low = 0.5;
    // float p_voltage_high = 4.5;
    // float p_voltage_scale = p_voltage_high - p_voltage_low;

    // float kpa_in_v = p_voltage_scale / range;
    // fpressure = (adc - p_voltage_low) / kpa_in_v + lower_range_limit;

    // Linear regression coefficients
    float a = 3.9628e-6;
    float b = -4.9509;

    // Calculate the manometer value in bar
    fpressure = a * fadc + b;

    Serial.print("fpressure: "); Serial.println(fpressure);

    return fpressure;
}



int16_t PressureSensor::getMaxPressure() {
    return 20000;
}
