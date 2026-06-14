#pragma once

#include "ScaleDevice.h"
#include <BLEUUID.h>

class LfSmartScaleDevice : public ScaleDevice {
public:
  static constexpr const char *kDeviceNameMatch = "lfsmart scale";
  static const BLEUUID kDataService;
  static const BLEUUID kDataCharacteristic;

  static bool matches(BLEAdvertisedDevice &device);
  static ScaleDevice *create(const BLEAddress &address, esp_ble_addr_type_t addrType,
                             const std::string &name, BLEClient *client);

  bool connect() override;
  void disconnect() override;
  bool isConnected() const override;
  float getWeight() const override;
  const char *getName() const override;
  void handleDisconnect();

private:
  LfSmartScaleDevice(BLEClient *client, const BLEAddress &address, esp_ble_addr_type_t addrType,
                     const std::string &name);

  static void notifyCallback(BLERemoteCharacteristic *characteristic, uint8_t *data, size_t length,
                             bool isNotify);
  void handleNotify(const uint8_t *data, size_t length);

  static LfSmartScaleDevice *s_activeInstance;

  BLEClient *m_client;
  BLEAddress m_address;
  esp_ble_addr_type_t m_addrType;
  std::string m_name;
  float m_weight;
  bool m_connected;
  BLERemoteCharacteristic *m_dataCharacteristic;
  BLEClientCallbacks *m_clientCallbacks;
};
