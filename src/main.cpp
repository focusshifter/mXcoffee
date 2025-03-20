#include "ui/m5unified_backend.h"
#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <M5Unified.h>
#include <Wire.h>
#include <string>

#include "ble/BLEBattery.h"
#include "ble/OEPLog.h"
#include "ble/OEPPressure.h"
#include "constants.h"
#include "pressure_sensor/pressure_sensor.h"
#include "ui/display_wrapper.h"
#include "ui/ui.h"

const int16_t PRESSURE_GRID_VALUES[] = {9, 6, 3, 0};
const unsigned long AUTO_OFF_TIMEOUT = 10 * 60 * 1000;

M5GFX display;
DisplayWrapper *displayWrapper = nullptr;
M5UnifiedCanvas m5Canvas(&M5.Display);
CanvasWrapper *canvas = &m5Canvas;
UI ui(canvas);
PressureSensor *pressureSensor = nullptr;
BLEBattery *bleBattery = nullptr;
OEPLog *bleLog = nullptr;
OEPPressure *blePressure = nullptr;

DeviceState deviceState = {.isAsleep = false,
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
                           .pServer = nullptr};

int16_t pressureValues[PRESSURE_VALUES_LEN];

#define VERSION "0.0.1"
#define DEBUG

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) override {
    deviceState.deviceConnected = true;
  }
  void onDisconnect(BLEServer *pServer) override {
    deviceState.deviceConnected = false;
    pServer->startAdvertising();
  }
};

void setup() {
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
  Serial.println("Setup: M5 initialized");

  display.fillScreen(TFT_BLACK);
  displayWrapper = new DisplayWrapper(display);

  Serial.print("Setup: Display width=");
  Serial.print(display.width());
  Serial.print(", height=");
  Serial.println(display.height());

  String build = String("m5stack version ") + VERSION + " " + __DATE__ + " " +
                 __TIME__ + " :)";
  displayWrapper->drawString(build, 10, 10);
  displayWrapper->drawCenterString("Ready to brew!",
                                   displayWrapper->width() / 2,
                                   displayWrapper->height() / 2);
  Serial.println("Setup: Display initialized");

  M5.Power.setExtOutput(true);
  Wire.begin(33, 32);
  M5.Ex_I2C.begin(I2C_NUM_0, 33, 32);
  Serial.println("Setup: I2C initialized");

  pressureSensor = new PressureSensor(&M5.Ex_I2C);
  Serial.println("Setup: Pressure sensor initialized");

  delay(150); // Use delay for consistency
  pressureSensor->getPressure();
  delay(150);
  Serial.println("Setup: First pressure reading done");

  for (int i = 0; i < PRESSURE_VALUES_LEN; i++)
    pressureValues[i] = 0;
  Serial.println("Setup: Pressure values array initialized");

  // Clear the display before entering loop()
  display.fillScreen(TFT_BLACK);
  Serial.println("Setup: Display cleared before loop");

  Serial.println("Setup: Complete");
}

void initBle() {
  if (deviceState.pServer == nullptr) {
    Serial.println("Creating new pServer");
    bleLog = new OEPLog();
    bleBattery = new BLEBattery(100);
    blePressure = new OEPPressure();

    BLEDevice::init("PRS-mXcoffee");
    BLEServer *pServer = BLEDevice::createServer();
    deviceState.pServer = pServer;
    pServer->setCallbacks(new MyServerCallbacks());

    bleBattery->setupBatteryService(pServer);
    bleLog->registerWithServer(pServer);
    blePressure->registerWithServer(pServer);
  }
  BLEAdvertising *pAdvertising =
      static_cast<BLEServer *>(deviceState.pServer)->getAdvertising();
  pAdvertising->start();
  deviceState.isBluetoothOn = true;
}

void deinitBle() {
  BLEAdvertising *pAdvertising =
      static_cast<BLEServer *>(deviceState.pServer)->getAdvertising();
  pAdvertising->stop();
  deviceState.isBluetoothOn = false;
}

int16_t getPressure() {
  int16_t pressure = pressureSensor->getPressure();
  for (int i = 0; i < PRESSURE_VALUES_LEN - 1; i++) {
    pressureValues[i] = pressureValues[i + 1];
  }
  pressureValues[PRESSURE_VALUES_LEN - 1] = pressure;
  return pressure;
}

void sendToBle(int16_t pressure) {
  if (deviceState.isBluetoothOn) {
    if (deviceState.deviceConnected) {
      blePressure->updatePressure(pressure);
      deviceState.lastBTSendSuccessful = true;
    } else {
      deviceState.lastBTSendSuccessful = false;
    }
  }
}

void playBtOnSound() {
  // Placeholder: Add sound logic if needed
}

void playBtOffSound() {
  // Placeholder: Add sound logic if needed
}

void updateShotTotalTime() {
  if (deviceState.isTimerRunning) {
    unsigned long currentTime = millis();
    deviceState.shotTotalTime += currentTime - deviceState.timerStartTime;
    deviceState.timerStartTime = currentTime;
  }
}

void setTimer(int16_t pressure) {
  unsigned long currentTime = millis();
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
  Serial.println("Loop: Start");
  M5.update();
  if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed()) {
    deviceState.lastActivityTime = millis();
    Serial.println("Loop: Button pressed");
  }
  if (!deviceState.isAsleep &&
      (millis() - deviceState.lastActivityTime >= AUTO_OFF_TIMEOUT)) {
    M5.Power.powerOff();
    Serial.println("Loop: Powering off due to inactivity");
    return;
  }
  delay(2); // Use delay for consistency

  if (deviceState.lastRefreshTime + 20 < millis()) {
    Serial.println("Loop: Refreshing display");
    deviceState.lastRefreshTime = millis();
    int16_t currentPressure = getPressure();
    if (deviceState.lastPressure == -1 ||
        currentPressure != deviceState.lastPressure) {
      deviceState.lastActivityTime = millis();
      deviceState.lastPressure = currentPressure;
    }
    setTimer(currentPressure);
    sendToBle(currentPressure);

    UIData data = {.pressureValues = {0},
                   .lastPressure = currentPressure,
                   .hexData = pressureSensor->getHexData(),
                   .isBluetoothOn = deviceState.isBluetoothOn,
                   .lastBTSendSuccessful = deviceState.lastBTSendSuccessful,
                   .batteryLevel = M5.Power.getBatteryLevel(),
                   .debugMode = deviceState.debugMode,
                   .shotTotalTime = deviceState.shotTotalTime,
                   .displayWidth = static_cast<int16_t>(M5.Display.width()),
                   .displayHeight = static_cast<int16_t>(M5.Display.height()),
                   .maxPressure = pressureSensor->getMaxPressure(),
                   .deviceConnected = deviceState.deviceConnected};
    std::copy(pressureValues, pressureValues + PRESSURE_VALUES_LEN,
              data.pressureValues);
    ui.draw(data);
    Serial.println("Loop: Display updated");
  }

  if (M5.BtnA.wasPressed()) {
    deviceState.debugMode = !deviceState.debugMode;
    Serial.println("Loop: Debug mode toggled");
  }
  if (M5.BtnB.wasPressed()) {
    deviceState.isBluetoothOn = !deviceState.isBluetoothOn;
    if (deviceState.isBluetoothOn) {
      initBle();
      String btStatus =
          String("Bluetooth is ") + (deviceState.isBluetoothOn ? "on" : "off");
      displayWrapper->drawCenterString(btStatus, displayWrapper->width() / 2,
                                       displayWrapper->height() / 2);
      playBtOnSound();
    } else {
      deinitBle();
      String btStatus =
          String("Bluetooth is ") + (deviceState.isBluetoothOn ? "on" : "off");
      displayWrapper->drawCenterString(btStatus, displayWrapper->width() / 2,
                                       displayWrapper->height() / 2);
      playBtOffSound();
    }
    Serial.println("Loop: Bluetooth toggled");
  }
}