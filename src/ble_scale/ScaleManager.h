#pragma once

#include "LfSmartScale.h"
#include <BLEDevice.h>
#include <BLEScan.h>
#include <string>

class ScaleManager : public BLEAdvertisedDeviceCallbacks {
public:
  ScaleManager();

  enum class ScaleType {
    Unknown = 0,
    LfSmartScale
  };

  void begin();
  void setBluetoothEnabled(bool enabled);
  void poll(uint32_t nowMs);

  bool isBluetoothEnabled() const;
  bool isScaleConnected() const;
  float getWeight() const;
  const char *getScaleName() const;
  std::string getNearbyScalesSummary() const;

  void onResult(BLEAdvertisedDevice advertisedDevice) override;

private:
  struct ScaleCandidate {
    BLEAddress address = BLEAddress((uint8_t *)"\0\0\0\0\0\0");
    esp_ble_addr_type_t addressType = BLE_ADDR_TYPE_PUBLIC;
    std::string name;
    int rssi = -1000;
    uint32_t lastSeenMs = 0;
    ScaleType type = ScaleType::Unknown;
  };

  static constexpr size_t kMaxCandidates = 6;
  static constexpr uint32_t kScanIntervalMs = 4000;
  static constexpr uint32_t kScanDurationSec = 2;

  static void scanCompleteCallback(BLEScanResults results);
  void handleScanComplete();

  void startScan(uint32_t nowMs);
  void stopScan();
  void pruneCandidates(uint32_t nowMs);
  void recordCandidate(BLEAdvertisedDevice &device, ScaleType type, uint32_t nowMs);
  void tryConnectCandidate();
  void tryConnectLastKnown(uint32_t nowMs);
  void clearActiveScale();

  static ScaleManager *s_instance;

  BLEScan *m_scan = nullptr;
  bool m_scanning = false;
  bool m_btEnabled = false;
  uint32_t m_lastScanMs = 0;

  ScaleCandidate m_candidates[kMaxCandidates];
  size_t m_candidateCount = 0;

  BLEClient *m_client = nullptr;
  ScaleDevice *m_activeScale = nullptr;
  bool m_hasLastKnown = false;
  ScaleCandidate m_lastKnown;
  uint32_t m_lastConnectAttemptMs = 0;
  bool m_pendingConnect = false;

#if defined(SHOT_WEIGHT_SIMULATED) && SHOT_WEIGHT_SIMULATED
  float m_simulatedWeight = 0.0f;
  uint32_t m_simulatedStartMs = 0;
  uint32_t m_simulatedLastMs = 0;
#endif
};
