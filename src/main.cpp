#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <M5Unified.h>
#include <Wire.h>
#include <string>
#include <vector>
#include <FS.h>
#include <SPIFFS.h>
#include "ble/BLEBattery.h"
#include "ble/OEPLog.h"
#include "ble/OEPPressure.h"
#include "constants.h"
#include "pressure_sensor/pressure_sensor.h"
#include "ui/display_wrapper.h"
#include "ui/ui.h"
#include "ble_scale/ScaleManager.h"


const int16_t PRESSURE_GRID_VALUES[] = {9, 6, 3, 0};
const unsigned long AUTO_OFF_TIMEOUT = 10 * 60 * 1000;

M5GFX display;
DisplayWrapper *displayWrapper = nullptr;
UI ui(&display);
PressureSensor *pressureSensor = nullptr;
BLEBattery *bleBattery = nullptr;
OEPLog *bleLog = nullptr;
OEPPressure *blePressure = nullptr;
ScaleManager scaleManager;

DeviceState defaultDeviceState = {
    .isAsleep = false,
    .isBluetoothOn = false,
    .deviceConnected = false,
    .lastBTSendSuccessful = false,
    .debugMode = false,
    .lastRefreshTime = 0,
    .lastActivityTime = 0,
    .lastWeightUpdateTime = 0,
    .lastPressure = -1,
    .timerStartTime = 0,
    .shotStartTime = 0,
    .shotTotalTime = 0,
    .isTimerRunning = false,
    .pServer = nullptr,
    .shotWeight = 0.0f,
    .lastShotWeight = 0.0f,
    .flowRate = 0.0f,
    .lastScaleSampleTime = 0,
    .flowCalcStartTime = 0,
    .flowCalcStartWeight = 0.0f
};

DeviceState deviceState = defaultDeviceState;

int16_t pressureValues[PRESSURE_VALUES_LEN];
std::vector<int16_t> pressureHistory;
std::vector<uint32_t> pressureHistoryTimes;

#define VERSION "0.0.1"
#define DEBUG

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) override { deviceState.deviceConnected = true; }
  void onDisconnect(BLEServer *pServer) override {
    deviceState.deviceConnected = false;
    pServer->startAdvertising();
  }
};

bool bluetoothInitialized = false;

void ensureBluetoothInit() {
  if (!bluetoothInitialized) {
    BLEDevice::init("PRS-mXcoffee");
    bluetoothInitialized = true;
  }
}

void resetBleState(bool releaseMemory) {
  if (bleLog) {
    delete bleLog;
    bleLog = nullptr;
  }
  if (bleBattery) {
    delete bleBattery;
    bleBattery = nullptr;
  }
  if (blePressure) {
    delete blePressure;
    blePressure = nullptr;
  }
  deviceState.pServer = nullptr;
  if (bluetoothInitialized) {
    BLEDevice::deinit(releaseMemory);
    bluetoothInitialized = false;
  }
}

void resetPressureHistory() {
  pressureHistory.clear();
  pressureHistoryTimes.clear();
}

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

  display.setColorDepth(24);
  display.setSwapBytes(false);

  if (!displayWrapper) {
    displayWrapper = new DisplayWrapper(display);
  }

  Serial.begin(115200);
  Serial.println("Setup: M5 initialized");

  display.fillScreen(THEME_DARKBG);

  String build = String("m5stack version ") + VERSION + " " + __DATE__ + " " + __TIME__ + " :)";
  display.drawString(build, 10, 10);
  display.drawCenterString("Ready to brew!", display.width() / 2, display.height() / 2);
  Serial.println("Setup: Display initialized");

  M5.Power.setExtOutput(true);
  Wire.begin(33, 32);
  M5.Ex_I2C.begin(I2C_NUM_0, 33, 32);
  Serial.println("Setup: I2C initialized");

  pressureSensor = new PressureSensor(&M5.Ex_I2C);
  Serial.println("Setup: Pressure sensor initialized");

  delay(150);
  pressureSensor->getPressure();
  delay(150);
  Serial.println("Setup: First pressure reading done");

  for (int i = 0; i < PRESSURE_VALUES_LEN; i++) pressureValues[i] = 0;
  resetPressureHistory();
  pressureHistory.reserve(6000);
  pressureHistoryTimes.reserve(6000);
  Serial.println("Setup: Pressure values array initialized");

  Serial.print("PSRAM available: ");
  Serial.println(psramFound() ? "Yes" : "No");
  Serial.print("Free heap: ");
  Serial.println(ESP.getFreeHeap());

  Serial.println("Setup: Sprite created");

  Serial.println("Setup: Complete");
}

// BLE and other functions remain unchanged
void initBle() {
  ensureBluetoothInit();
  if (deviceState.pServer == nullptr) {
    Serial.println("Creating new pServer");
    bleLog = new OEPLog();
    bleBattery = new BLEBattery(100);
    blePressure = new OEPPressure();

    BLEServer *pServer = BLEDevice::createServer();
    deviceState.pServer = pServer;
    pServer->setCallbacks(new MyServerCallbacks());

    bleBattery->setupBatteryService(pServer);
    bleLog->registerWithServer(pServer);
    blePressure->registerWithServer(pServer);
  }
  BLEAdvertising *pAdvertising = static_cast<BLEServer *>(deviceState.pServer)->getAdvertising();
  pAdvertising->start();
  deviceState.isBluetoothOn = true;
}

void deinitBle() {
  if (deviceState.pServer != nullptr) {
    BLEAdvertising *pAdvertising = static_cast<BLEServer *>(deviceState.pServer)->getAdvertising();
    pAdvertising->stop();
  }
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

void playBtOnSound() {}
void playBtOffSound() {}

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
      if (deviceState.shotStartTime == 0) {
        deviceState.shotStartTime = currentTime;
      }
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
  if (!deviceState.isAsleep && (millis() - deviceState.lastActivityTime >= AUTO_OFF_TIMEOUT)) {
    M5.Power.powerOff();
    Serial.println("Loop: Powering off due to inactivity");
    return;
  }
  delay(2);

  if (deviceState.lastRefreshTime + 5 < millis()) {
    deviceState.lastRefreshTime = millis();
    int16_t currentPressure = getPressure();
    if (deviceState.lastPressure == -1 || currentPressure != deviceState.lastPressure) {
      deviceState.lastActivityTime = millis();
      deviceState.lastPressure = currentPressure;
    }
    setTimer(currentPressure);
    sendToBle(currentPressure);

    unsigned long currentTime = millis();
    if (deviceState.shotStartTime == 0) {
      deviceState.shotStartTime = currentTime;
    }
    pressureHistory.push_back(currentPressure);
    pressureHistoryTimes.push_back(currentTime - deviceState.shotStartTime);

    scaleManager.poll(currentTime);
    if (scaleManager.isScaleConnected()) {
      float scaleWeight = scaleManager.getWeight();
      deviceState.lastScaleSampleTime = currentTime;

      if (deviceState.lastWeightUpdateTime == 0) {
        deviceState.shotWeight = scaleWeight;
        deviceState.lastShotWeight = scaleWeight;
        deviceState.lastWeightUpdateTime = currentTime;
        deviceState.flowCalcStartTime = currentTime;
        deviceState.flowCalcStartWeight = scaleWeight;
      } else {
        float timeDelta = (currentTime - deviceState.lastWeightUpdateTime) / 1000.0f;
        if (timeDelta >= 0.2f) {
          deviceState.lastShotWeight = deviceState.shotWeight;
          deviceState.shotWeight = scaleWeight;
          deviceState.lastWeightUpdateTime = currentTime;
        }

        unsigned long flowWindowMs = 1000;
        if (currentTime - deviceState.flowCalcStartTime >= flowWindowMs) {
          float flowDeltaSeconds =
              (currentTime - deviceState.flowCalcStartTime) / 1000.0f;
          float weightDelta = deviceState.shotWeight - deviceState.flowCalcStartWeight;
          if (flowDeltaSeconds > 0.0f) {
            deviceState.flowRate = weightDelta / flowDeltaSeconds;
          }
          deviceState.flowCalcStartTime = currentTime;
          deviceState.flowCalcStartWeight = deviceState.shotWeight;
        }
      }

      if (currentTime - deviceState.lastScaleSampleTime > 2000) {
        deviceState.flowRate = 0.0f;
      }
    } else {
      deviceState.lastWeightUpdateTime = 0;
      deviceState.flowRate = 0.0f;
      deviceState.shotWeight = 0.0f;
      deviceState.lastShotWeight = 0.0f;
      deviceState.lastScaleSampleTime = 0;
      deviceState.flowCalcStartTime = 0;
      deviceState.flowCalcStartWeight = 0.0f;
    }

    UIData data = {
        .pressureValues = {0},
        .pressureHistory = pressureHistory.data(),
        .pressureHistoryTimes = pressureHistoryTimes.data(),
        .pressureHistoryCount = pressureHistory.size(),
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
        .deviceConnected = deviceState.deviceConnected,
        .shotWeight = deviceState.shotWeight,
        .flowRate = deviceState.flowRate,
        .scaleConnected = scaleManager.isScaleConnected(),
        .scaleName = scaleManager.getScaleName(),
        .nearbyScales = scaleManager.getNearbyScalesSummary()
    };
    std::copy(pressureValues, pressureValues + PRESSURE_VALUES_LEN, data.pressureValues);
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
      scaleManager.setBluetoothEnabled(true);
      String btStatus = String("Bluetooth is ") + (deviceState.isBluetoothOn ? "on" : "off");
      if (displayWrapper) {
        displayWrapper->drawCenterString(btStatus, displayWrapper->width() / 2, displayWrapper->height() / 2);
      }
      playBtOnSound();
    } else {
      deinitBle();
      scaleManager.setBluetoothEnabled(false);
      String btStatus = String("Bluetooth is ") + (deviceState.isBluetoothOn ? "on" : "off");
      if (displayWrapper) {
        displayWrapper->drawCenterString(btStatus, displayWrapper->width() / 2, displayWrapper->height() / 2);
      }
      playBtOffSound();
    }
    Serial.println("Loop: Bluetooth toggled");
  }
  if (M5.BtnC.wasPressed()) {
    // Reset state
    deviceState = defaultDeviceState;
    std::fill(pressureValues, pressureValues + PRESSURE_VALUES_LEN, 0);
    resetPressureHistory();
    deinitBle();
    scaleManager.setBluetoothEnabled(false);
    resetBleState(false);
    Serial.println("Loop: State reset");
    delay(1000);
  }
}
