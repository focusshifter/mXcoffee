
#include "pressure_sensor.h"

PressureSensor::PressureSensor() {
    pressure = 0;
}

void PressureSensor::start() {
    pressure = 0;
}

void PressureSensor::stop() {   
    pressure = 0;
}

int16_t PressureSensor::getPressure() {
    pressure = int16_t(random(0, 20000));
    return pressure;
}

int16_t PressureSensor::getMaxPressure() {
    return 20000;
}

