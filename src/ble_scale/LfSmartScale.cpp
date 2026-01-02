#include "LfSmartScale.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLERemoteCharacteristic.h>
#include <BLERemoteService.h>
#include <algorithm>
#include <cctype>

const BLEUUID LfSmartScaleDevice::kDataService("FFF0");
const BLEUUID LfSmartScaleDevice::kDataCharacteristic("FFF4");
LfSmartScaleDevice *LfSmartScaleDevice::s_activeInstance = nullptr;

namespace {
class LfScaleClientCallbacks : public BLEClientCallbacks {
public:
  explicit LfScaleClientCallbacks(LfSmartScaleDevice *device) : m_device(device) {}

  void onConnect(BLEClient *) override {}

  void onDisconnect(BLEClient *) override {
    if (m_device) {
      m_device->handleDisconnect();
    }
  }

private:
  LfSmartScaleDevice *m_device;
};
} // namespace

static std::string toLower(const std::string &value) {
  std::string out = value;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
  return out;
}

bool LfSmartScaleDevice::matches(BLEAdvertisedDevice &device) {
  if (device.haveName()) {
    return toLower(device.getName()).find(kDeviceNameMatch) != std::string::npos;
  }
  return false;
}

ScaleDevice *LfSmartScaleDevice::create(const BLEAddress &address, esp_ble_addr_type_t addrType,
                                        const std::string &name, BLEClient *client) {
  return new LfSmartScaleDevice(client, address, addrType, name);
}

LfSmartScaleDevice::LfSmartScaleDevice(BLEClient *client, const BLEAddress &address,
                                       esp_ble_addr_type_t addrType, const std::string &name)
    : m_client(client),
      m_address(address),
      m_addrType(addrType),
      m_name(name),
      m_weight(0.0f),
      m_connected(false),
      m_dataCharacteristic(nullptr),
      m_clientCallbacks(nullptr) {}

bool LfSmartScaleDevice::connect() {
  if (!m_client) {
    return false;
  }
  if (!m_clientCallbacks) {
    m_clientCallbacks = new LfScaleClientCallbacks(this);
    m_client->setClientCallbacks(m_clientCallbacks);
  }
  if (!m_client->connect(m_address, m_addrType)) {
    return false;
  }

  BLERemoteService *service = m_client->getService(kDataService);
  if (!service) {
    m_client->disconnect();
    return false;
  }

  m_dataCharacteristic = service->getCharacteristic(kDataCharacteristic);
  if (!m_dataCharacteristic) {
    m_client->disconnect();
    return false;
  }

  bool canNotify = m_dataCharacteristic->canNotify();
  bool canIndicate = m_dataCharacteristic->canIndicate();
  if (!canNotify && !canIndicate) {
    m_client->disconnect();
    return false;
  }

  s_activeInstance = this;
  m_dataCharacteristic->registerForNotify(LfSmartScaleDevice::notifyCallback, canNotify, true);
  m_connected = true;
  return true;
}

void LfSmartScaleDevice::disconnect() {
  if (m_client && m_client->isConnected()) {
    m_client->disconnect();
  }
  if (s_activeInstance == this) {
    s_activeInstance = nullptr;
  }
  m_connected = false;
}

bool LfSmartScaleDevice::isConnected() const { return m_connected && m_client && m_client->isConnected(); }

float LfSmartScaleDevice::getWeight() const { return m_weight; }

const char *LfSmartScaleDevice::getName() const { return m_name.c_str(); }

void LfSmartScaleDevice::notifyCallback(BLERemoteCharacteristic *, uint8_t *data, size_t length,
                                        bool) {
  if (s_activeInstance) {
    s_activeInstance->handleNotify(data, length);
  }
}

void LfSmartScaleDevice::handleNotify(const uint8_t *data, size_t length) {
  if (length < 6) {
    return;
  }
  int16_t raw = static_cast<int16_t>((data[4] << 8) | data[3]);
  float weight = static_cast<float>(raw) / 10.0f;
  if (data[5] > 0) {
    weight *= -1.0f;
  }
  m_weight = weight;
}

void LfSmartScaleDevice::handleDisconnect() {
  m_connected = false;
  if (s_activeInstance == this) {
    s_activeInstance = nullptr;
  }
}
