#include <Arduino.h>
#include <M5Unified.h>
#include <Wire.h>
#include <FS.h>
#include <SPIFFS.h>
#include <cmath>
#include <string>
#include <vector>
#include "ble/MxBleServer.h"
#include "ble_scale/ScaleManager.h"
#include "constants.h"
#include "pressure_sensor/pressure_sensor.h"
#include "settings/Settings.h"
#include "ui/display_wrapper.h"
#include "ui/ui.h"


const int16_t PRESSURE_GRID_VALUES[] = {9, 6, 3, 0};
const unsigned long AUTO_OFF_TIMEOUT = 10 * 60 * 1000;
const unsigned long SCALE_STALE_TIMEOUT = 5000;

M5GFX display;
DisplayWrapper *displayWrapper = nullptr;
UI ui(&display);
PressureSensor *pressureSensor = nullptr;
mxcoffee::ble::MxBleServer bleServer;
ScaleManager scaleManager;
Settings settings;

DeviceState defaultDeviceState = {
    .isAsleep = false,
    .isBluetoothOn = false,
    .deviceConnected = false,
    .lastBTSendSuccessful = false,
    .debugMode = false,
    .lastRefreshTime = 0,
    .lastActivityTime = 0,
    .lastWeightUpdateTime = 0,
    .lastGraphChangeTime = 0,
    .lastPressure = -1,
    .lastGraphPressure = -1,
    .lastGraphWeight = 0.0f,
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
    .flowCalcStartWeight = 0.0f,
    .screenshotBtnPressTime = 0,
    .screenshotPending = false
};

DeviceState deviceState = defaultDeviceState;

int16_t pressureValues[PRESSURE_VALUES_LEN];
std::vector<int16_t> pressureHistory;
std::vector<int16_t> weightHistory;
std::vector<uint32_t> pressureHistoryTimes;

#define VERSION "0.0.1"
#define DEBUG

void initBle();
void deinitBle();
void restoreSavedScale();
void resetSessionState();

void saveLastScale(const ScaleManager::SavedScale &scale) {
  Settings::LastScaleConfig config;
  snprintf(config.address, sizeof(config.address), "%s", scale.address.c_str());
  snprintf(config.name, sizeof(config.name), "%s", scale.name.c_str());
  config.addressType = static_cast<uint8_t>(scale.addressType);
  config.scaleType = static_cast<uint8_t>(scale.type);
  settings.saveLastScale(config);
}

void restoreSavedScale() {
  Settings::LastScaleConfig config;
  if (!settings.loadLastScale(config)) {
    return;
  }

  ScaleManager::SavedScale savedScale;
  savedScale.address = config.address;
  savedScale.name = config.name;
  savedScale.addressType = static_cast<esp_ble_addr_type_t>(config.addressType);
  savedScale.type = static_cast<ScaleManager::ScaleType>(config.scaleType);
  scaleManager.restoreLastKnownScale(savedScale);
}

void resetBleState(bool releaseMemory) {
  bleServer.reset(releaseMemory);
  deviceState.deviceConnected = false;
  deviceState.lastBTSendSuccessful = false;
  deviceState.pServer = nullptr;
}

void resetPressureHistory() {
  pressureHistory.clear();
  weightHistory.clear();
  pressureHistoryTimes.clear();
}

void resetSessionState() {
  deviceState.lastRefreshTime = 0;
  deviceState.lastActivityTime = millis();
  deviceState.lastWeightUpdateTime = 0;
  deviceState.lastGraphChangeTime = 0;
  deviceState.lastPressure = -1;
  deviceState.lastGraphPressure = -1;
  deviceState.lastGraphWeight = 0.0f;
  deviceState.timerStartTime = 0;
  deviceState.shotStartTime = 0;
  deviceState.shotTotalTime = 0;
  deviceState.isTimerRunning = false;
  deviceState.shotWeight = scaleManager.isScaleConnected() ? scaleManager.getWeight() : 0.0f;
  deviceState.lastShotWeight = deviceState.shotWeight;
  deviceState.flowRate = 0.0f;
  deviceState.lastScaleSampleTime = 0;
  deviceState.flowCalcStartTime = 0;
  deviceState.flowCalcStartWeight = deviceState.shotWeight;
  deviceState.screenshotBtnPressTime = 0;
  deviceState.screenshotPending = false;
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

  ui.drawSplash();
  Serial.println("Setup: Display initialized");

  M5.Power.setExtOutput(true);
  Wire.begin(33, 32);
  M5.Ex_I2C.begin(I2C_NUM_0, 33, 32);
  Serial.println("Setup: I2C initialized");

  pressureSensor = new PressureSensor(&M5.Ex_I2C);
  Serial.println("Setup: Pressure sensor initialized");

  if (!settings.begin()) {
    Serial.println("Setup: Settings initialization failed");
  } else {
    scaleManager.setLastKnownScaleChangedCallback(saveLastScale);
    restoreSavedScale();
    deviceState.isBluetoothOn = settings.getBluetoothEnabled();
    Serial.printf("Setup: Bluetooth state loaded: %s\n", deviceState.isBluetoothOn ? "ON" : "OFF");
  }

  if (deviceState.isBluetoothOn) {
    initBle();
    scaleManager.setBluetoothEnabled(true);
    Serial.println("Setup: Bluetooth auto-enabled on boot");
  }

  delay(150);
  pressureSensor->getPressure();
  delay(150);
  Serial.println("Setup: First pressure reading done");

  for (int i = 0; i < PRESSURE_VALUES_LEN; i++) pressureValues[i] = 0;
  resetPressureHistory();
  pressureHistory.reserve(6000);
  weightHistory.reserve(6000);
  pressureHistoryTimes.reserve(6000);
  Serial.println("Setup: Pressure values array initialized");

  Serial.print("PSRAM available: ");
  Serial.println(psramFound() ? "Yes" : "No");
  Serial.print("Free heap: ");
  Serial.println(ESP.getFreeHeap());

  Serial.println("Setup: Sprite created");

  Serial.println("Setup: Complete");
}

void initBle() {
  if (!bleServer.begin(M5.Power.getBatteryLevel())) {
    Serial.println("BLE: Failed to initialize");
    deviceState.isBluetoothOn = false;
    return;
  }

  bleServer.startAdvertising();
  deviceState.deviceConnected = bleServer.isConnected();
  deviceState.isBluetoothOn = true;
}

void deinitBle() {
  bleServer.stopAdvertising();
  deviceState.deviceConnected = false;
  deviceState.lastBTSendSuccessful = false;
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
    deviceState.deviceConnected = bleServer.isConnected();
    deviceState.lastBTSendSuccessful = bleServer.notifyPressure(pressure);
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
    int32_t batteryLevel = M5.Power.getBatteryLevel();
    if (deviceState.isBluetoothOn) {
      bleServer.setBatteryLevel(batteryLevel);
      deviceState.deviceConnected = bleServer.isConnected();
    }

    unsigned long currentTime = millis();

    scaleManager.poll(currentTime);
    bool scaleConnected = scaleManager.isScaleConnected();
    if (scaleConnected) {
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

    } else {
      bool scaleRecentlyActive =
          deviceState.lastScaleSampleTime != 0 &&
          currentTime - deviceState.lastScaleSampleTime <= SCALE_STALE_TIMEOUT;
      deviceState.flowRate = 0.0f;
      if (!scaleRecentlyActive) {
        deviceState.lastWeightUpdateTime = 0;
        deviceState.shotWeight = 0.0f;
        deviceState.lastShotWeight = 0.0f;
        deviceState.lastScaleSampleTime = 0;
        deviceState.flowCalcStartTime = 0;
        deviceState.flowCalcStartWeight = 0.0f;
      }
    }
    bool scaleVisible =
        scaleConnected ||
        (deviceState.lastScaleSampleTime != 0 &&
         currentTime - deviceState.lastScaleSampleTime <= SCALE_STALE_TIMEOUT);

    float currentWeight = deviceState.shotWeight;
    if (deviceState.lastGraphChangeTime == 0) {
      deviceState.lastGraphChangeTime = currentTime;
      deviceState.lastGraphPressure = currentPressure;
      deviceState.lastGraphWeight = currentWeight;
    } else if (currentPressure != deviceState.lastGraphPressure ||
               fabsf(currentWeight - deviceState.lastGraphWeight) > 0.01f) {
      deviceState.lastGraphChangeTime = currentTime;
      deviceState.lastGraphPressure = currentPressure;
      deviceState.lastGraphWeight = currentWeight;
    }

    if (deviceState.shotStartTime == 0) {
      deviceState.shotStartTime = currentTime;
    }

    if (currentTime - deviceState.lastGraphChangeTime < 1000) {
      pressureHistory.push_back(currentPressure);
      int16_t weightValue = static_cast<int16_t>(currentWeight * 10.0f);
      weightHistory.push_back(weightValue);
      pressureHistoryTimes.push_back(currentTime - deviceState.shotStartTime);
    }

    UIData data = {
        .pressureValues = {0},
        .pressureHistory = pressureHistory.data(),
        .weightHistory = weightHistory.data(),
        .pressureHistoryTimes = pressureHistoryTimes.data(),
        .pressureHistoryCount = pressureHistory.size(),
        .weightHistoryCount = weightHistory.size(),
        .lastPressure = currentPressure,
        .hexData = pressureSensor->getHexData(),
        .isBluetoothOn = deviceState.isBluetoothOn,
        .lastBTSendSuccessful = deviceState.lastBTSendSuccessful,
        .batteryLevel = batteryLevel,
        .debugMode = deviceState.debugMode,
        .shotTotalTime = deviceState.shotTotalTime,
        .displayWidth = static_cast<int16_t>(M5.Display.width()),
        .displayHeight = static_cast<int16_t>(M5.Display.height()),
        .maxPressure = pressureSensor->getMaxPressure(),
        .deviceConnected = deviceState.deviceConnected,
        .shotWeight = deviceState.shotWeight,
        .flowRate = deviceState.flowRate,
        .scaleConnected = scaleVisible,
        .scaleName = scaleManager.getScaleName(),
        .nearbyScales = scaleManager.getNearbyScalesSummary()
    };
    std::copy(pressureValues, pressureValues + PRESSURE_VALUES_LEN, data.pressureValues);
    ui.draw(data);
    if (deviceState.screenshotPending) {
      deviceState.screenshotPending = false;
      char filename[32];
      snprintf(filename, sizeof(filename), "/screenshot_%lu.bmp", millis());
      ui.captureScreenshot(filename);
    }
    Serial.println("Loop: Display updated");
  }

  if (M5.BtnA.wasPressed()) {
    deviceState.screenshotBtnPressTime = millis();
  }
  if (M5.BtnA.isPressed() && deviceState.screenshotBtnPressTime > 0) {
    if (millis() - deviceState.screenshotBtnPressTime >= 1000) {
      deviceState.screenshotPending = true;
      deviceState.screenshotBtnPressTime = 0;
      if (displayWrapper) {
        displayWrapper->drawCenterString("Screenshot...", displayWrapper->width() / 2, displayWrapper->height() / 2);
      }
      Serial.println("Loop: Screenshot triggered");
    }
  }
  if (M5.BtnA.wasReleased() && deviceState.screenshotBtnPressTime > 0) {
    if (millis() - deviceState.screenshotBtnPressTime < 1000) {
      deviceState.debugMode = !deviceState.debugMode;
      Serial.println("Loop: Debug mode toggled");
    }
    deviceState.screenshotBtnPressTime = 0;
  }
  if (M5.BtnB.wasPressed()) {
    deviceState.isBluetoothOn = !deviceState.isBluetoothOn;
    if (deviceState.isBluetoothOn) {
      initBle();
      restoreSavedScale();
      scaleManager.setBluetoothEnabled(true);
      settings.setBluetoothEnabled(true);
      String btStatus = String("Bluetooth is ") + (deviceState.isBluetoothOn ? "on" : "off");
      if (displayWrapper) {
        displayWrapper->drawCenterString(btStatus, displayWrapper->width() / 2, displayWrapper->height() / 2);
      }
      playBtOnSound();
    } else {
      deinitBle();
      scaleManager.setBluetoothEnabled(false);
      settings.setBluetoothEnabled(false);
      String btStatus = String("Bluetooth is ") + (deviceState.isBluetoothOn ? "on" : "off");
      if (displayWrapper) {
        displayWrapper->drawCenterString(btStatus, displayWrapper->width() / 2, displayWrapper->height() / 2);
      }
      playBtOffSound();
    }
    Serial.println("Loop: Bluetooth toggled");
  }
  if (M5.BtnC.wasPressed()) {
    resetSessionState();
    std::fill(pressureValues, pressureValues + PRESSURE_VALUES_LEN, 0);
    resetPressureHistory();
    scaleManager.reset();
    if (pressureSensor) {
      pressureSensor->resetSimulation();
    }
    Serial.println("Loop: Session state reset");
    delay(1000);
  }
}
