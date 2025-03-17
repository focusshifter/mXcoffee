#include <Arduino.h>
#ifdef SIMULATOR
 #include "ui/simulator_backend.cpp"
#else
 #include <M5Unified.h>
 #include "ui/m5unified_backend.cpp"
#endif
#include "ui/ui.h"
#include "ui/display_wrapper.h"
#include "ble/OEPPressure.h"
#include "ble/OEPLog.h"
#include "ble/BLEBattery.h"
#include "pressure_sensor/pressure_sensor.h"
#include "constants.h"
#include <Wire.h>
#include <string>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Pressure grid values
const int16_t PRESSURE_GRID_VALUES[] = {9, 6, 3, 0};

// Auto-off timer duration (10 minutes in milliseconds)
const unsigned long AUTO_OFF_TIMEOUT = 10 * 60 * 1000;

M5GFX display;
DisplayWrapper* displayWrapper = nullptr; // Defined here
UI ui; // Defined here
PressureSensor* pressureSensor = nullptr;
BLEBattery* bleBattery = nullptr;
OEPLog* bleLog = nullptr;
OEPPressure* blePressure = nullptr;

DeviceState deviceState = {
 .isAsleep = false,
 .isBluetoothOn = false,
 .deviceConnected = false,
 .lastBTSendSuccessful = false,
 .debugMode = false,
 .lastRefreshTime = 0,
 .lastActivityTime = 0,
 .lastPressure = -1,
 .timerStartTime = 0,
 .shotTotalTime = 0,
 .isTimerRunning = false,
 .pServer = nullptr
};

int16_t pressureValues[PRESSURE_VALUES_LEN];

#ifdef SIMULATOR
 #include "ui/simulator_backend.cpp" // Define simulator globals here
#else
 M5UnifiedCanvas m5Canvas(&M5.Display);
 CanvasWrapper* canvas = &m5Canvas; // Defined here
 M5UnifiedSystem m5System;
 SystemWrapper* sysWrapper = &m5System; // Defined here
#endif

#define VERSION "0.0.1"

// #define DEBUG

class MyServerCallbacks : public BLEServerCallbacks {
 void onConnect(BLEServer* pServer) { deviceState.deviceConnected = true; }
 void onDisconnect(BLEServer* pServer) {
 deviceState.deviceConnected = false;
 pServer->startAdvertising();
 }
};

void setup() {
#ifdef SIMULATOR
 Serial.begin(115200);
 pressureSensor = new PressureSensor(nullptr);
#else
 auto cfg = M5.config();
 cfg.serial_baudrate = 115200;
 cfg.internal_imu = true;
 cfg.internal_rtc = true;
 cfg.internal_spk = true;
 cfg.internal_mic = false;
 cfg.external_imu = false;
 cfg.external_rtc = false;
 M5.begin(cfg);
 M5.Speaker.begin();
 M5.Speaker.setVolume(120);
 display = M5.Lcd;
 Serial.begin(115200);

 display.fillScreen(TFT_BLACK);
 displayWrapper = new DisplayWrapper(display);

 String build = String("m5stack version ") + VERSION + " " + __DATE__ + " " + __TIME__ + " :)";
 displayWrapper->drawString(build, 10, 10);
 displayWrapper->drawCenterString("Ready to brew!", displayWrapper->width() / 2, displayWrapper->height() / 2);

 M5.Power.setExtOutput(true);
 Wire.begin(33, 32);
 M5.Ex_I2C.begin(I2C_NUM_0, 33, 32);
 pressureSensor = new PressureSensor(&M5.Ex_I2C);

 M5.delay(150);
 pressureSensor->getPressure();
 M5.delay(150);
#endif
 for (int i = 0; i < PRESSURE_VALUES_LEN; i++) pressureValues[i] = 0;
 ui.drawGraph();
}

void initBle() {
#ifndef SIMULATOR
 if (deviceState.pServer == nullptr) {
 Serial.println("Creating new pServer");
 bleLog = new OEPLog();
 bleBattery = new BLEBattery(100);
 blePressure = new OEPPressure();

 BLEDevice::init("PRS-mXcoffee");
 deviceState.pServer = BLEDevice::createServer();
 deviceState.pServer->setCallbacks(new MyServerCallbacks());

 bleBattery->setupBatteryService(deviceState.pServer);
 bleLog->registerWithServer(deviceState.pServer);
 blePressure->registerWithServer(deviceState.pServer);
 }
 BLEAdvertising* pAdvertising = deviceState.pServer->getAdvertising();
 pAdvertising->start();
 deviceState.isBluetoothOn = true;
#endif
}

void deinitBle() {
#ifndef SIMULATOR
 BLEAdvertising* pAdvertising = deviceState.pServer->getAdvertising();
 pAdvertising->stop();
 deviceState.isBluetoothOn = false;
#endif
}

int16_t getPressure() {
#ifdef DEBUG
 static uint32_t lastTime = 0;
 static int16_t pressure = 0;
 if (M5.millis() - lastTime > 30) {
 lastTime = M5.millis();
 double normalized_time = 2.0 * M_PI * (M5.millis() / 10000.0);
 double sin_value = sin(normalized_time - M_PI / 2);
 pressure = int16_t(round(0.5 * (sin_value + 1.0) * 12000));
 }
#else
 int16_t pressure = pressureSensor->getPressure();
#endif
 for (int i = 0; i < PRESSURE_VALUES_LEN - 1; i++) {
 pressureValues[i] = pressureValues[i + 1];
 }
 pressureValues[PRESSURE_VALUES_LEN - 1] = pressure;
 return pressure;
}

void sendToBle(int16_t pressure) {
#ifndef SIMULATOR
 if (deviceState.isBluetoothOn) {
 if (deviceState.deviceConnected) {
 blePressure->updatePressure(pressure);
 deviceState.lastBTSendSuccessful = true;
 } else {
 deviceState.lastBTSendSuccessful = false;
 }
 }
#endif
}

void playBtOnSound() {
#ifndef SIMULATOR
 // Placeholder
#endif
}

void playBtOffSound() {
#ifndef SIMULATOR
 // Placeholder
#endif
}

void updateShotTotalTime() {
 if (deviceState.isTimerRunning) {
 unsigned long currentTime = M5.millis();
 deviceState.shotTotalTime += currentTime - deviceState.timerStartTime;
 deviceState.timerStartTime = currentTime;
 }
}

void setTimer(int16_t pressure) {
 unsigned long currentTime = M5.millis();
 if (pressure > 1000) {
 if (!deviceState.isTimerRunning) {
 deviceState.isTimerRunning = true;
 deviceState.timerStartTime = currentTime;
 } else {
 updateShotTotalTime();
 }
 } else {
 if (deviceState.isTimerRunning) {
 updateShotTotalTime();
 deviceState.isTimerRunning = false;
 }
 }
}

void loop() {
#ifdef SIMULATOR
 SDL_Event e;
 while (SDL_PollEvent(&e)) if (e.type == SDL_QUIT) exit(0);
#else
 M5.update();
 if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed()) {
 deviceState.lastActivityTime = millis();
 }
 if (!deviceState.isAsleep && (millis() - deviceState.lastActivityTime >= AUTO_OFF_TIMEOUT)) {
 M5.Power.powerOff();
 return;
 }
 M5.delay(2);
#endif
 if (deviceState.lastRefreshTime + 20 < M5.millis()) {
 deviceState.lastRefreshTime = M5.millis();
 int16_t currentPressure = getPressure();
 if (deviceState.lastPressure == -1 || currentPressure != deviceState.lastPressure) {
 deviceState.lastActivityTime = millis();
 deviceState.lastPressure = currentPressure;
 }
 setTimer(currentPressure);
 sendToBle(currentPressure);
 ui.drawGraph();
 }
#ifndef SIMULATOR
 if (M5.BtnA.wasPressed()) {
 deviceState.debugMode = !deviceState.debugMode;
 }
 if (M5.BtnB.wasPressed()) {
 deviceState.isBluetoothOn = !deviceState.isBluetoothOn;
 if (deviceState.isBluetoothOn) {
 initBle();
 String btStatus = "Bluetooth is " + String(deviceState.isBluetoothOn ? "on" : "off");
 displayWrapper->drawCenterString(btStatus, displayWrapper->width() / 2, displayWrapper->height() / 2);
 playBtOnSound();
 } else {
 deinitBle();
 String btStatus = "Bluetooth is " + String(deviceState.isBluetoothOn ? "on" : "off");
 displayWrapper->drawCenterString(btStatus, displayWrapper->width() / 2, displayWrapper->height() / 2);
 playBtOffSound();
 }
 }
 if (M5.BtnC.wasPressed()) {
 displayWrapper->drawCenterString("Rebooting", displayWrapper->width() / 2, displayWrapper->height() / 2);
 M5.delay(200);
 ESP.restart();
 }
#endif
}

#ifdef SIMULATOR
int main() {
 setup();
 while (true) loop();
 return 0;
}
#endif