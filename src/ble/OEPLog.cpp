#include "OEPLog.h"
#include <BLE2902.h>

#ifdef SIMULATOR
#include "SimulatorArduino.h"
#else
#include <Arduino.h>
#endif

BLECharacteristic LogCharacteristic(BLE_LOG_CHARACTERISTIC,
                                    BLECharacteristic::PROPERTY_NOTIFY);
BLEDescriptor LogDescriptor(BLE_LOG_DESCRIPTOR);

void OEPLog::registerWithServer(BLEServer *pServer) {
  auto logService = pServer->createService(BLE_LOG_SERVICE);
  LogDescriptor.setValue("null terminated string");
  logService->addCharacteristic(&LogCharacteristic);
  LogCharacteristic.addDescriptor(&LogDescriptor);
  LogCharacteristic.addDescriptor(new BLE2902());
  pServer->getAdvertising()->addServiceUUID(BLE_LOG_SERVICE);
  logService->start();
}

void OEPLog::log(const std::string &str) {
#ifdef SIMULATOR
  Serial_println(str.c_str());
#else
  Serial.println(str.c_str());
#endif
  LogCharacteristic.setValue(str);
  LogCharacteristic.notify();
}

OEPLog::OEPLog() = default;