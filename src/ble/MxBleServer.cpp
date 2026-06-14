#include "MxBleServer.h"

#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

namespace mxcoffee::ble {

namespace {
BLEDescriptor *createDescription(const char *value) {
  auto *descriptor = new BLEDescriptor(BLEUUID((uint16_t)ESP_GATT_UUID_CHAR_DESCRIPTION));
  descriptor->setValue(value);
  return descriptor;
}
} // namespace

class MxBleServer::ServerCallbacks : public BLEServerCallbacks {
public:
  explicit ServerCallbacks(MxBleServer *owner) : m_owner(owner) {}

  void onConnect(BLEServer *) override {
    Serial.println("BLE: client connected");
    if (m_owner) {
      m_owner->setConnected(true);
    }
  }

  void onDisconnect(BLEServer *server) override {
    Serial.println("BLE: client disconnected; restarting advertising");
    if (m_owner) {
      m_owner->setConnected(false);
    }
    server->startAdvertising();
  }

private:
  MxBleServer *m_owner;
};

class MxBleServer::ZeroCallbacks : public BLECharacteristicCallbacks {
public:
  explicit ZeroCallbacks(MxBleServer *owner) : m_owner(owner) {}

  void onWrite(BLECharacteristic *) override {
    Serial.println("BLE: zero pressure requested");
    if (m_owner) {
      m_owner->zeroPressure();
    }
  }

private:
  MxBleServer *m_owner;
};

MxBleServer::~MxBleServer() { reset(false); }

bool MxBleServer::begin(uint8_t initialBatteryLevel) {
  if (m_server) {
    setBatteryLevel(initialBatteryLevel);
    return true;
  }

  BLEDevice::init(kDeviceName);
  m_bluetoothInitialized = true;
  Serial.printf("BLE: initialized as %s\n", kDeviceName);

  m_server = BLEDevice::createServer();
  if (!m_server) {
    return false;
  }

  m_serverCallbacks = new ServerCallbacks(this);
  m_server->setCallbacks(m_serverCallbacks);

  registerBatteryService(initialBatteryLevel);
  registerLogService();
  registerPressureService();
  Serial.println("BLE: services registered");
  return true;
}

void MxBleServer::startAdvertising() {
  if (m_server) {
    m_server->getAdvertising()->start();
    Serial.println("BLE: advertising started");
  }
}

void MxBleServer::stopAdvertising() {
  if (m_server) {
    m_server->getAdvertising()->stop();
    Serial.println("BLE: advertising stopped");
  }
}

void MxBleServer::reset(bool releaseMemory) {
  stopAdvertising();
  m_connected = false;
  m_server = nullptr;
  m_batteryCharacteristic = nullptr;
  m_logCharacteristic = nullptr;
  m_pressureCharacteristic = nullptr;
  m_zeroCharacteristic = nullptr;

  if (m_bluetoothInitialized) {
    BLEDevice::deinit(releaseMemory);
    m_bluetoothInitialized = false;
  }

  delete m_serverCallbacks;
  m_serverCallbacks = nullptr;
  delete m_zeroCallbacks;
  m_zeroCallbacks = nullptr;
  m_pressureState = PressureState();
}

bool MxBleServer::notifyPressure(int16_t pressure) {
  if (!m_pressureCharacteristic) {
    return false;
  }

  m_pressureState.updatePressure(pressure);
  PressurePayload payload = m_pressureState.encodedPressure();
  m_pressureCharacteristic->setValue(payload.data(), payload.size());

  if (!m_connected) {
    if (!m_loggedNoPressureClient) {
      Serial.println("BLE: pressure updated but no client is connected");
      m_loggedNoPressureClient = true;
    }
    return false;
  }

  m_pressureCharacteristic->notify();
  return true;
}

bool MxBleServer::setBatteryLevel(int level) {
  if (!m_batteryCharacteristic) {
    return false;
  }

  uint8_t batteryLevel = clampBatteryLevel(level);
  m_batteryCharacteristic->setValue(&batteryLevel, 1);
  if (m_connected) {
    m_batteryCharacteristic->notify();
  }
  return true;
}

bool MxBleServer::log(const std::string &message) {
  if (!m_logCharacteristic) {
    return false;
  }

  Serial.println(message.c_str());
  const std::string payload = logPayload(message);
  m_logCharacteristic->setValue(payload);
  if (!m_connected) {
    return false;
  }

  m_logCharacteristic->notify();
  return true;
}

bool MxBleServer::isConnected() const { return m_connected; }

void MxBleServer::registerBatteryService(uint8_t initialBatteryLevel) {
  BLEService *service = m_server->createService(BLEUUID((uint16_t)kBatteryServiceUuid));
  m_batteryCharacteristic = service->createCharacteristic(
      BLEUUID((uint16_t)kBatteryLevelCharacteristicUuid),
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  m_batteryCharacteristic->addDescriptor(createDescription("1..100%"));
  m_batteryCharacteristic->addDescriptor(new BLE2902());
  service->start();
  m_server->getAdvertising()->addServiceUUID(BLEUUID((uint16_t)kBatteryServiceUuid));
  setBatteryLevel(initialBatteryLevel);
}

void MxBleServer::registerLogService() {
  BLEService *service = m_server->createService(BLEUUID(kLogServiceUuid));
  m_logCharacteristic = service->createCharacteristic(
      BLEUUID(kLogCharacteristicUuid), BLECharacteristic::PROPERTY_NOTIFY);
  m_logCharacteristic->addDescriptor(createDescription("null terminated string"));
  m_logCharacteristic->addDescriptor(new BLE2902());
  service->start();
  m_server->getAdvertising()->addServiceUUID(BLEUUID(kLogServiceUuid));
}

void MxBleServer::registerPressureService() {
  BLEService *service = m_server->createService(BLEUUID(kPressureServiceUuid));
  m_pressureCharacteristic = service->createCharacteristic(
      BLEUUID(kPressureCharacteristicUuid),
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  m_pressureCharacteristic->addDescriptor(
      createDescription("notify: pressure followed by temperature at every 16th notification"));
  m_pressureCharacteristic->addDescriptor(new BLE2902());

  m_zeroCharacteristic = service->createCharacteristic(
      BLEUUID(kPressureZeroCharacteristicUuid), BLECharacteristic::PROPERTY_WRITE);
  m_zeroCharacteristic->addDescriptor(createDescription("write: any value"));
  m_zeroCallbacks = new ZeroCallbacks(this);
  m_zeroCharacteristic->setCallbacks(m_zeroCallbacks);

  service->start();
  m_server->getAdvertising()->addServiceUUID(BLEUUID(kPressureServiceUuid));
}

void MxBleServer::setConnected(bool connected) {
  m_connected = connected;
  m_loggedNoPressureClient = false;
}

void MxBleServer::zeroPressure() { m_pressureState.zero(); }

} // namespace mxcoffee::ble
