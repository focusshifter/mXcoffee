#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <Arduino.h>
#include <BLEDevice.h>

// Pressure sensor constants
#define PRESSURE_VALUES_LEN 160

// Pressure grid constants
#define PRESSURE_GRID_COUNT 4

// Device state structure
struct DeviceState
{
  bool isAsleep;
  bool isBluetoothOn;
  bool deviceConnected;
  bool lastBTSendSuccessful;
  bool debugMode;
  unsigned long lastRefreshTime;
  unsigned long lastActivityTime;  // Track last activity time
  int16_t lastPressure;            // Track last pressure reading

  unsigned long timerStartTime;  // Timer start time
  unsigned long shotTotalTime;   // Total shot time
  bool isTimerRunning;

  BLEServer* pServer;
};

// External declarations
class PressureSensor;
extern PressureSensor* pressureSensor;
extern DeviceState deviceState;
extern const int16_t PRESSURE_GRID_VALUES[];
extern int16_t pressureValues[PRESSURE_VALUES_LEN];  // Added extern declaration

#endif  // CONSTANTS_H