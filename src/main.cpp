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

M5GFX display;
PressureSensor pressureSensor;

BLEBattery *bleBattery;
OEPLog *bleLog;
OEPPressure *blePressure;

#define PRESSURE_VALUES_LEN 160
int16_t pressureValues[PRESSURE_VALUES_LEN];

bool deviceConnected = false;

#define VERSION "0.0.1"

//Setup callbacks onConnect and onDisconnect
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
    };
    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        pServer->startAdvertising();
    }
};

void setup() {
  M5.begin();
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

  Serial.println("creating log");
  bleLog = new OEPLog();
  Serial.println("creating battery");
  bleBattery = new BLEBattery(100);
  Serial.println("creating pressure");
  blePressure = new OEPPressure();

  BLEDevice::init("PRS-m5flair");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  Serial.println("Attaching battery service");
  bleBattery->setupBatteryService(pServer);

  Serial.println("Attaching logger service");
  bleLog->registerWithServer(pServer);

  Serial.println("Attaching pressure service");
  blePressure->registerWithServer(pServer);
  
  Serial.println("Begin BT advertising");
  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->start();
}

int16_t getPressure() {
  int16_t pressure = pressureSensor.getPressure();

  for (int i = 0; i < PRESSURE_VALUES_LEN - 1; i++) {
    pressureValues[i] = pressureValues[i + 1];
  }
  pressureValues[PRESSURE_VALUES_LEN - 1] = pressure;

  return pressure;
}

void sendToBle(int16_t pressure) {
  if (deviceConnected) {
    blePressure->updatePressure(pressure);
    display.drawString("Sent to BLE", 10, display.height() - 35);
  } else {
    display.drawString("BT not connected", 10, display.height() - 35);
  }
}

void drawGraph() {
  const int16_t graphHeight = display.height() - 40;
  const int16_t graphWidth = display.width() - 20;
  const int16_t graphStartX = 10;
  const int16_t graphStartY = 30;
  

  int16_t minPressure = 0;
  int16_t maxPressure = pressureSensor.getMaxPressure();

  int16_t minPressureY = graphStartY + graphHeight;
  int16_t maxPressureY = graphStartY;

  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.drawString("Pressure (bar)", 10, 10);

  display.drawLine(graphStartX, maxPressureY, graphWidth, maxPressureY, TFT_WHITE);
  display.drawLine(graphStartX, minPressureY, graphWidth, minPressureY, TFT_WHITE);

  for (int i = 0; i < PRESSURE_VALUES_LEN; i++) {
    int16_t pressure1 = pressureValues[i - 1];
    int16_t pressure2 = pressureValues[i];
    int16_t pressureY1 = graphStartY + graphHeight - pressure1 * graphHeight / pressureSensor.getMaxPressure();
    int16_t pressureY2 = graphStartY + graphHeight - pressure2 * graphHeight / pressureSensor.getMaxPressure();
    int16_t pressureX1 = graphStartX + (i - 1) * graphWidth / PRESSURE_VALUES_LEN;
    int16_t pressureX2 = graphStartX + i * graphWidth / PRESSURE_VALUES_LEN;
    display.drawLine(pressureX1, pressureY1, pressureX2, pressureY2, TFT_GREENYELLOW);
  }
}

void loop() {
  M5.delay(1000);
  M5.update();

  int16_t pressure = getPressure();
  // sendToBle(pressure);

  drawGraph();

  // display.drawCenterString("Pressure: " + String(double(pressure) / 1000.0) + " bar", display.width() / 2, display.height() / 2);

  // if (M5.BtnA.wasPressed()) {
  //   display.drawCenterString("Button A was pressed!", display.width() / 2, display.height() / 2);
  //   M5.Speaker.tone(1000, 100, -1, true);
  // }

  // if (M5.BtnB.wasPressed()) {
  //   display.drawCenterString("Button B was pressed!", display.width() / 2, display.height() / 2);
  //   M5.Speaker.tone(1000, 100, -1, true);
  // }

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
