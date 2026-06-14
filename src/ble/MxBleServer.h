#pragma once

#include "BleProtocol.h"

#include <cstdint>
#include <string>

class BLECharacteristic;
class BLEServer;

namespace mxcoffee::ble {

class MxBleServer {
public:
  MxBleServer() = default;
  ~MxBleServer();

  bool begin(uint8_t initialBatteryLevel);
  void startAdvertising();
  void stopAdvertising();
  void reset(bool releaseMemory);

  bool notifyPressure(int16_t pressure);
  bool setBatteryLevel(int level);
  bool log(const std::string &message);

  bool isConnected() const;

private:
  class ServerCallbacks;
  class ZeroCallbacks;

  void registerBatteryService(uint8_t initialBatteryLevel);
  void registerLogService();
  void registerPressureService();
  void setConnected(bool connected);
  void zeroPressure();

  BLEServer *m_server = nullptr;
  BLECharacteristic *m_batteryCharacteristic = nullptr;
  BLECharacteristic *m_logCharacteristic = nullptr;
  BLECharacteristic *m_pressureCharacteristic = nullptr;
  BLECharacteristic *m_zeroCharacteristic = nullptr;
  ServerCallbacks *m_serverCallbacks = nullptr;
  ZeroCallbacks *m_zeroCallbacks = nullptr;
  PressureState m_pressureState;
  bool m_bluetoothInitialized = false;
  bool m_connected = false;
  bool m_loggedNoPressureClient = false;
};

} // namespace mxcoffee::ble
