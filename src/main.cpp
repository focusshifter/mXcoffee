#include <Arduino.h>
#include <M5Unified.h>
#include <M5GFX.h>
#include <string>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "ble/OEPPressure.h"
#include "ble/OEPLog.h"
#include "ble/BLEBattery.h"
#include "pressure_sensor/pressure_sensor.h"

#include <Wire.h>

M5GFX display;
PressureSensor *pressureSensor;

BLEBattery *bleBattery;
OEPLog *bleLog;
OEPPressure *blePressure;

struct DeviceState {
    bool isAsleep;
    bool isBluetoothOn;
    bool deviceConnected;
    bool lastBTSendSuccessful;
    bool debugMode;
    unsigned long lastRefreshTime;

    unsigned long timerStartTime;   // Timer start time
    unsigned long shotTotalTime;    // Total shot time
    bool isTimerRunning;    

    BLEServer *pServer;
};

DeviceState deviceState = {
    .isAsleep = false,
    .isBluetoothOn = false,
    .deviceConnected = false,
    .lastBTSendSuccessful = false,
    .debugMode = false,
    .lastRefreshTime = 0,
    .pServer = nullptr
};

#define PRESSURE_VALUES_LEN 160
int16_t pressureValues[PRESSURE_VALUES_LEN];

#define VERSION "0.0.1"

// #define DEBUG

//Setup callbacks onConnect and onDisconnect
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceState.deviceConnected = true;
    };
    void onDisconnect(BLEServer* pServer) {
        deviceState.deviceConnected = false;
        pServer->startAdvertising();
    }
};

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;   // default=115200. if "Serial" is not needed, set it to 0.
  cfg.internal_imu = true;        // default=true. use internal IMU.
  cfg.internal_rtc = true;        // default=true. use internal RTC.
  cfg.internal_spk = true;        // default=true. use internal speaker.
  cfg.internal_mic = false;       // default=true. use internal microphone.
  cfg.external_imu = false;       // default=false. use Unit Accel & Gyro.
  cfg.external_rtc = false;       // default=false. use Unit RTC.
  M5.begin(cfg);
  M5.Speaker.begin();
  M5.Speaker.setVolume(120);
  display = M5.Lcd;
  Serial.begin(115200);

  display.fillScreen(TFT_BLACK);

  String build = String("m5stack version ")
    + VERSION + " "
    + __DATE__ + " "
    + __TIME__ + " :)";

  display.drawString(build, 10, 10);
  display.drawCenterString("Ready to brew!", display.width() / 2, display.height() / 2);

  // Initialize I2C pressure sensor
  M5.Power.setExtOutput(true); // Turn on I2C power
  Wire.begin(33, 32);
  M5.Ex_I2C.begin(I2C_NUM_0, 33, 32);

  pressureSensor = new PressureSensor(&M5.Ex_I2C);

  M5.delay(150);
  pressureSensor->getPressure(); // Read once to initialize sensor
  M5.delay(150);
}

void initBle() {
  if (deviceState.pServer == nullptr) {
    Serial.println("Creating new pServer");

    Serial.println("Creating log");
    bleLog = new OEPLog();
    Serial.println("Creating battery");
    bleBattery = new BLEBattery(100);
    Serial.println("Creating pressure");
    blePressure = new OEPPressure();

    // Pretend that we're PRS-compatible device by name
    BLEDevice::init("PRS-mXcoffee");

    deviceState.pServer = BLEDevice::createServer();
    deviceState.pServer->setCallbacks(new MyServerCallbacks());
    
    Serial.println("Attaching battery service");
    bleBattery->setupBatteryService(deviceState.pServer);

    Serial.println("Attaching logger service");
    bleLog->registerWithServer(deviceState.pServer);

    Serial.println("Attaching pressure service");
    blePressure->registerWithServer(deviceState.pServer);
  } else {
    Serial.println("Reusing existing pServer");
  }
    
  Serial.println("Begin BT advertising");
  BLEAdvertising *pAdvertising = deviceState.pServer->getAdvertising();
  pAdvertising->start();
  deviceState.isBluetoothOn = true;
}

void deinitBle() {
  Serial.println("Stop BT advertising");
  BLEAdvertising *pAdvertising = deviceState.pServer->getAdvertising();
  pAdvertising->stop();
  deviceState.isBluetoothOn = false;
}

int16_t getPressure() {

#ifdef DEBUG
    Serial.println("Debug: generating fake pressure");

    double normalized_time = 2.0 * M_PI * (M5.millis() / 10000.0);
    double sin_value = sin(normalized_time - M_PI / 2);
    
    int16_t pressure = int16_t(round(0.5 * (sin_value + 1.0) * 15000));
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
  if (deviceState.isBluetoothOn) {
    if (deviceState.deviceConnected) {
      blePressure->updatePressure(pressure);
      // display.drawString("Sent to BLE", 10, display.height() - 35);
      deviceState.lastBTSendSuccessful = true;
    } else {
      // display.drawString("BT not connected", 10, display.height() - 35);
      deviceState.lastBTSendSuccessful = false;
    }
  }
}

void drawGraph() {
  const int16_t graphStartX = 10;
  const int16_t graphStartY = 60;
  const int16_t graphHeight = display.height() - graphStartY - 10;
  const int16_t graphWidth = display.width() - graphStartX * 2;
  
  int16_t lastPressure = pressureValues[PRESSURE_VALUES_LEN - 1];
  String hex_data = pressureSensor->getHexData();

  int graphColor;
  bool showPressureWarning = false;

  if(lastPressure > 12000) {
    graphColor = TFT_RED;
    showPressureWarning = true;
  } else if(lastPressure > 9000) {
    graphColor = TFT_YELLOW;
  } else if(lastPressure > 6000) {
    graphColor = TFT_GREEN;
  } else {
    graphColor = TFT_GREEN;
  }

  int16_t minPressure = 0;
  int16_t maxSensorPressure = pressureSensor->getMaxPressure(); // 20000 mbar
  int16_t maxPressure = maxSensorPressure - 5000; // 15000 mbar

  int16_t minPressureY = graphStartY + graphHeight;
  int16_t maxPressureY = graphStartY;
  
  M5Canvas graph(&display);

  graph.createSprite(display.width(), display.height());

  graph.fillSprite(TFT_BLACK);

  // Draw BT state
  if (deviceState.isBluetoothOn) {
    if (deviceState.lastBTSendSuccessful) {
      graph.setTextColor(TFT_GREEN, TFT_BLACK);
    } else {
      graph.setTextColor(TFT_RED, TFT_BLACK);
    }
  } else {
    graph.setTextColor(TFT_DARKGRAY, TFT_BLACK);
  }
  graph.drawString("BT", display.width() - 50, 10);

  // Draw battery state
  int32_t batteryLevel = M5.Power.getBatteryLevel();
  if (batteryLevel > 25) {
    graph.setTextColor(TFT_GREEN, TFT_BLACK);
  } else if (batteryLevel > 15) {
    graph.setTextColor(TFT_YELLOW, TFT_BLACK);
  } else if (batteryLevel > 5) {
    graph.setTextColor(TFT_ORANGE, TFT_BLACK);
  } else {
    graph.setTextColor(TFT_RED, TFT_BLACK);
  }
  graph.drawRightString(String(batteryLevel) + "%", display.width() - 10, 10);

  graph.drawLine(graphStartX, maxPressureY, graphStartX + graphWidth, maxPressureY, TFT_WHITE);
  graph.drawLine(graphStartX, minPressureY, graphStartX + graphWidth, minPressureY, TFT_WHITE);

  graph.setTextColor(TFT_WHITE);

  for (int i = 0; i < 6; i++) {
    int16_t pressureY = graphStartY + graphHeight - i * graphHeight / 5;
    graph.drawLine(graphStartX, pressureY, graphStartX + graphWidth, pressureY, TFT_DARKGRAY);
    graph.drawString(String(i * maxPressure / 5 / 1000), 0, pressureY);
  }

  for (int i = 1; i < PRESSURE_VALUES_LEN; i++) {
    int16_t pressure1 = pressureValues[i - 1];
    int16_t pressure2 = pressureValues[i];
    int16_t pressureY1 = graphStartY + graphHeight - pressure1 * graphHeight / maxPressure;
    int16_t pressureY2 = graphStartY + graphHeight - pressure2 * graphHeight / maxPressure;
    int16_t pressureX1 = graphStartX + (i - 1) * graphWidth / PRESSURE_VALUES_LEN;
    int16_t pressureX2 = graphStartX + i * graphWidth / PRESSURE_VALUES_LEN;
    graph.drawLine(pressureX1, pressureY1, pressureX2, pressureY2, graphColor);
  }

  // Screen header
  graph.setFont(&DejaVu40);

  // Draw pressure in bar
  graph.setTextColor(graphColor);
  graph.drawRightString(String(float(lastPressure) / 1000), 260, 10);

  // Draw shot time in seconds
  graph.setTextColor(TFT_WHITE);
  float shotTime = float(deviceState.shotTotalTime) / 1000;
  graph.drawRightString(String(shotTime, 1) + "s", 130, 10);
  
  if (showPressureWarning) {
    graph.setTextColor(TFT_RED);
    graph.setFont(&DejaVu72);
    graph.drawCenterString("STOP!", 160, 120);
  }

  if (deviceState.debugMode) {
    graph.setTextColor(TFT_WHITE, TFT_BLACK);
    graph.setFont(&DejaVu12);
    graph.drawString("Pressure (bar): " + String(float(lastPressure) / 1000), 40, display.height()-60);
    graph.drawString("Last refresh: " + String(deviceState.lastRefreshTime), 40, display.height()-90);
    graph.drawString("Sensor raw data: " + hex_data, 40, display.height() - 30);
  }

  display.startWrite(); 
  graph.pushSprite(0, 0);
  display.endWrite();
}

// Simple i2c scanner for debugging
void i2c_scan() {
  byte error, address;
  int nDevices;
  Serial.println("Scanning...");
  nDevices = 0;
  for(address = 1; address < 127; address++ ) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("I2C device found at address 0x");
      if (address<16) Serial.print("0");
      Serial.println(address, HEX);
      nDevices++;
    }
    else if (error==4) {
      Serial.print("Unknown error at address 0x");
      if (address<16) Serial.print("0");
      Serial.println(address, HEX);
    }
  }
  if (nDevices == 0) Serial.println("No I2C devices found\n");
  else Serial.println("done\n");
  delay(5000); // Wait 5 seconds for next scan
}

void playBtOnSound() {
  M5.Speaker.tone(660, 100, -1, true);
  M5.delay(100);
  M5.Speaker.tone(880, 100, -1, true);
  M5.delay(100);
}

void playBtOffSound() {
  M5.Speaker.tone(880, 100, -1, true);
  M5.delay(100);
  M5.Speaker.tone(660, 100, -1, true);
  M5.delay(100);
}

void updateShotTotalTime() {
  if (deviceState.isTimerRunning) {
    unsigned long currentTime = M5.millis();
    deviceState.shotTotalTime += currentTime - deviceState.timerStartTime;
    deviceState.timerStartTime = currentTime; // Reset start time for next interval
  }
}

void setTimer(int16_t pressure) {
  unsigned long currentTime = M5.millis();

  if (pressure > 1000) {
    if (!deviceState.isTimerRunning) {
      // Start the timer
      deviceState.isTimerRunning = true;
      deviceState.timerStartTime = currentTime;
    } else {
      // Update the timer
      updateShotTotalTime();
    }
  } else {
    if (deviceState.isTimerRunning) {
      // Pause the timer and calculate the total shot time
      updateShotTotalTime();
      deviceState.isTimerRunning = false;
    }
  }
}

void loop() {
  // i2c_scan();

  M5.delay(2);
  M5.update();

  if (deviceState.lastRefreshTime + 20 < M5.millis()) {
    deviceState.lastRefreshTime = M5.millis();

    int16_t pressure = getPressure();

    setTimer(pressure);
    
    sendToBle(pressure);

    drawGraph();
  }

  if (M5.BtnA.wasPressed()) {
    deviceState.debugMode = !deviceState.debugMode;
  }

  if (M5.BtnB.wasPressed()) {
    deviceState.isBluetoothOn = !deviceState.isBluetoothOn;
    if (deviceState.isBluetoothOn) {
      initBle();
      display.drawCenterString("Bluetooth is " + String(deviceState.isBluetoothOn ? "on" : "off"), display.width() / 2, display.height() / 2);
      playBtOnSound();
    } else {
      deinitBle();
      display.drawCenterString("Bluetooth is " + String(deviceState.isBluetoothOn ? "on" : "off"), display.width() / 2, display.height() / 2);
      playBtOffSound();
    }
  }

  if (M5.BtnC.wasPressed()) {
    display.drawCenterString("Rebooting", display.width() / 2, display.height() / 2);
    M5.Speaker.tone(660, 100, -1, true);
    M5.delay(100);
    M5.Speaker.tone(880, 100, -1, true);
    M5.delay(100);
    M5.Speaker.tone(783, 100, -1, true);
    M5.delay(200);
    ESP.restart();
  }
}
