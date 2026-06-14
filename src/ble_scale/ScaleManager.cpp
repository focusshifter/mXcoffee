#include "ScaleManager.h"
#include <Arduino.h>
#include <algorithm>
#include <cmath>

ScaleManager *ScaleManager::s_instance = nullptr;

namespace {
struct ScaleFactory {
  bool (*matches)(BLEAdvertisedDevice &device);
  ScaleDevice *(*create)(const BLEAddress &address, esp_ble_addr_type_t addrType, const std::string &name,
                         BLEClient *client);
  ScaleManager::ScaleType type;
};

constexpr ScaleFactory kFactories[] = {
    {LfSmartScaleDevice::matches, LfSmartScaleDevice::create, ScaleManager::ScaleType::LfSmartScale},
};
} // namespace

ScaleManager::ScaleManager() {
  if (!s_instance) {
    s_instance = this;
  }
}

void ScaleManager::begin() {
  if (!m_scan) {
    m_scan = BLEDevice::getScan();
    m_scan->setAdvertisedDeviceCallbacks(this, true);
    m_scan->setActiveScan(true);
    m_scan->setInterval(1349);
    m_scan->setWindow(449);
  }
}

void ScaleManager::setBluetoothEnabled(bool enabled) {
  if (m_btEnabled == enabled && enabled) {
    return;
  }
  m_btEnabled = enabled;
  if (!m_btEnabled) {
    stopScan();
    clearActiveScale();
    m_candidateCount = 0;
    m_lastScanMs = 0;
    m_scan = nullptr;
    m_hasLastKnown = false;
    m_lastKnownFromStorage = false;
    m_lastConnectAttemptMs = 0;
    m_pendingConnect = false;
  } else {
    begin();
  }
}

void ScaleManager::poll(uint32_t nowMs) {
#if defined(SHOT_WEIGHT_SIMULATED) && SHOT_WEIGHT_SIMULATED
  if (m_simulatedStartMs == 0) {
    m_simulatedStartMs = nowMs;
    m_simulatedLastMs = nowMs;
  }

  float elapsedSeconds = (nowMs - m_simulatedStartMs) / 1000.0f;
  float flowRate = 0.0f;

#if defined(SHOT_WEIGHT_SIMULATED_SHOT) && SHOT_WEIGHT_SIMULATED_SHOT
  const float preinfusionSeconds = 6.0f;
  const float preinfusionRampSeconds = 0.5f;
  const float preinfusionTarget = 2.0f;
  const float rampUpSeconds = 1.0f;
  const float taperSeconds = 25.0f;
  const float rampDownSeconds = 1.0f;
  const float preinfusionFlow = preinfusionTarget / preinfusionSeconds;
  const float flowScale = 0.1995f;
  const float preinfusionBar = 2.0f;
  const float peakBar = 9.0f;
  const float endBar = 6.0f;

  if (elapsedSeconds < preinfusionSeconds) {
    if (elapsedSeconds < preinfusionRampSeconds) {
      float progress = elapsedSeconds / preinfusionRampSeconds;
      float eased = progress * progress;
      flowRate = preinfusionFlow * eased;
    } else {
      flowRate = preinfusionFlow;
    }
  } else {
    float t = elapsedSeconds - preinfusionSeconds;
    if (t < rampUpSeconds) {
      float progress = t / rampUpSeconds;
      float eased = progress * progress;
      float pressureBar = preinfusionBar + (peakBar - preinfusionBar) * eased;
      flowRate = pressureBar * flowScale;
    } else if (t < rampUpSeconds + taperSeconds) {
      float progress = (t - rampUpSeconds) / taperSeconds;
      float pressureBar = peakBar + (endBar - peakBar) * progress;
      flowRate = pressureBar * flowScale;
    } else if (t < rampUpSeconds + taperSeconds + rampDownSeconds) {
      float progress = (t - rampUpSeconds - taperSeconds) / rampDownSeconds;
      float eased = 1.0f - (1.0f - progress) * (1.0f - progress);
      float pressureBar = endBar + (0.0f - endBar) * eased;
      flowRate = pressureBar * flowScale;
    } else {
      flowRate = 0.0f;
    }
  }
#else
  const float cycleSeconds = 10.0f;
  const float activeSeconds = 6.0f;
  const float maxFlowRate = 12.0f;
  float cycleTime = fmodf(elapsedSeconds, cycleSeconds);
  float wave = 0.0f;
  if (cycleTime < activeSeconds) {
    wave = (1.0f - cosf(cycleTime * 3.14159265f / activeSeconds)) * 0.5f;
  }
  flowRate = wave * maxFlowRate;
#endif

  float deltaSeconds = (nowMs - m_simulatedLastMs) / 1000.0f;
  m_simulatedWeight += flowRate * deltaSeconds;
  m_simulatedLastMs = nowMs;
  if (m_simulatedWeight > 40.0f) {
    m_simulatedWeight = 40.0f;
  }
  return;
#endif

  if (!m_btEnabled) {
    return;
  }

  if (m_activeScale && !m_activeScale->isConnected()) {
    clearActiveScale();
  }

  if (m_activeScale) {
    if (m_scanning) {
      stopScan();
    }
    return;
  }

  pruneCandidates(nowMs);

  if (m_pendingConnect && !m_scanning && !m_activeScale && m_candidateCount > 0) {
    tryConnectCandidate();
    m_pendingConnect = false;
  }

  if (!m_scanning && !m_pendingConnect && nowMs - m_lastScanMs >= kScanIntervalMs) {
    startScan(nowMs);
  }

  if (!m_scanning && !m_activeScale && m_candidateCount == 0) {
    tryConnectLastKnown(nowMs);
  }
}

void ScaleManager::reset() {
#if defined(SHOT_WEIGHT_SIMULATED) && SHOT_WEIGHT_SIMULATED
  m_simulatedWeight = 0.0f;
  m_simulatedStartMs = 0;
  m_simulatedLastMs = 0;
#endif
}

void ScaleManager::setLastKnownScaleChangedCallback(LastKnownScaleChangedCallback callback) {
  m_lastKnownScaleChangedCallback = callback;
}

bool ScaleManager::restoreLastKnownScale(const SavedScale &scale) {
#if defined(SHOT_WEIGHT_SIMULATED) && SHOT_WEIGHT_SIMULATED
  return false;
#else
  if (scale.address.empty() || scale.type == ScaleType::Unknown) {
    return false;
  }

  m_lastKnown.address = BLEAddress(scale.address);
  m_lastKnown.addressType = scale.addressType;
  m_lastKnown.name = scale.name.empty() ? "Unknown" : scale.name;
  m_lastKnown.rssi = -1000;
  m_lastKnown.lastSeenMs = 0;
  m_lastKnown.type = scale.type;
  m_hasLastKnown = true;
  m_lastKnownFromStorage = true;
  Serial.printf("ScaleManager: Restored last scale %s (%s)\n",
                m_lastKnown.name.c_str(), m_lastKnown.address.toString().c_str());
  return true;
#endif
}

bool ScaleManager::getLastKnownScale(SavedScale &scale) {
#if defined(SHOT_WEIGHT_SIMULATED) && SHOT_WEIGHT_SIMULATED
  return false;
#else
  if (!m_hasLastKnown || m_lastKnown.type == ScaleType::Unknown) {
    return false;
  }

  scale.address = m_lastKnown.address.toString();
  scale.addressType = m_lastKnown.addressType;
  scale.name = m_lastKnown.name;
  scale.type = m_lastKnown.type;
  return true;
#endif
}

bool ScaleManager::isBluetoothEnabled() const { return m_btEnabled; }

bool ScaleManager::isScaleConnected() const {
#if defined(SHOT_WEIGHT_SIMULATED) && SHOT_WEIGHT_SIMULATED
  return true;
#else
  return m_activeScale && m_activeScale->isConnected();
#endif
}

float ScaleManager::getWeight() const {
#if defined(SHOT_WEIGHT_SIMULATED) && SHOT_WEIGHT_SIMULATED
  return m_simulatedWeight;
#else
  if (!m_activeScale) {
    return 0.0f;
  }
  return m_activeScale->getWeight();
#endif
}

const char *ScaleManager::getScaleName() const {
#if defined(SHOT_WEIGHT_SIMULATED) && SHOT_WEIGHT_SIMULATED
  return "SimScale";
#else
  if (!m_activeScale) {
    return "";
  }
  return m_activeScale->getName();
#endif
}

std::string ScaleManager::getNearbyScalesSummary() const {
#if defined(SHOT_WEIGHT_SIMULATED) && SHOT_WEIGHT_SIMULATED
  return "simulated";
#else
  if (!m_btEnabled) {
    return "bt off";
  }
  if (m_candidateCount == 0) {
    return "none";
  }
  std::string summary;
  size_t shown = 0;
  for (size_t i = 0; i < m_candidateCount && shown < 3; ++i) {
    if (m_candidates[i].name.empty()) {
      continue;
    }
    if (!summary.empty()) {
      summary += ", ";
    }
    summary += m_candidates[i].name;
    ++shown;
  }
  if (shown == 0) {
    return "unknown";
  }
  if (m_candidateCount > shown) {
    summary += " ...";
  }
  return summary;
#endif
}

void ScaleManager::onResult(BLEAdvertisedDevice advertisedDevice) {
  uint32_t nowMs = millis();
  for (const auto &factory : kFactories) {
    if (factory.matches(advertisedDevice)) {
      recordCandidate(advertisedDevice, factory.type, nowMs);
      break;
    }
  }
}

void ScaleManager::scanCompleteCallback(BLEScanResults) {
  if (s_instance) {
    s_instance->handleScanComplete();
  }
}

void ScaleManager::handleScanComplete() {
  m_scanning = false;
  if (m_scan) {
    m_scan->clearResults();
  }
  if (m_candidateCount > 0) {
    m_pendingConnect = true;
  }
}

void ScaleManager::startScan(uint32_t nowMs) {
  if (!m_scan || m_scanning) {
    return;
  }
  m_lastScanMs = nowMs;
  m_scanning = m_scan->start(kScanDurationSec, ScaleManager::scanCompleteCallback, false);
}

void ScaleManager::stopScan() {
  if (m_scan && m_scanning) {
    m_scan->stop();
  }
  m_scanning = false;
}

void ScaleManager::pruneCandidates(uint32_t nowMs) {
  const uint32_t staleMs = 15000;
  size_t writeIndex = 0;
  for (size_t i = 0; i < m_candidateCount; ++i) {
    if (nowMs - m_candidates[i].lastSeenMs <= staleMs) {
      if (writeIndex != i) {
        m_candidates[writeIndex] = m_candidates[i];
      }
      ++writeIndex;
    }
  }
  m_candidateCount = writeIndex;
}

void ScaleManager::recordCandidate(BLEAdvertisedDevice &device, ScaleType type, uint32_t nowMs) {
  BLEAddress address = device.getAddress();
  for (size_t i = 0; i < m_candidateCount; ++i) {
    if (m_candidates[i].address.equals(address)) {
      m_candidates[i].rssi = device.getRSSI();
      m_candidates[i].lastSeenMs = nowMs;
      if (device.haveName()) {
        m_candidates[i].name = device.getName();
      }
      m_candidates[i].type = type;
      if (m_hasLastKnown && m_lastKnown.address.equals(address)) {
        m_lastKnown = m_candidates[i];
        m_lastKnownFromStorage = false;
      }
      return;
    }
  }

  size_t insertIndex = m_candidateCount;
  if (insertIndex >= kMaxCandidates) {
    insertIndex = 0;
    uint32_t oldestAge = 0;
    for (size_t i = 0; i < m_candidateCount; ++i) {
      uint32_t age = nowMs - m_candidates[i].lastSeenMs;
      if (age > oldestAge) {
        oldestAge = age;
        insertIndex = i;
      }
    }
  } else {
    ++m_candidateCount;
  }

  m_candidates[insertIndex].address = address;
  m_candidates[insertIndex].addressType = device.getAddressType();
  m_candidates[insertIndex].rssi = device.getRSSI();
  m_candidates[insertIndex].lastSeenMs = nowMs;
  m_candidates[insertIndex].type = type;
  m_candidates[insertIndex].name = device.haveName() ? device.getName() : "Unknown";

  m_lastKnown = m_candidates[insertIndex];
  m_hasLastKnown = true;
  m_lastKnownFromStorage = false;
}

void ScaleManager::tryConnectCandidate() {
  if (m_activeScale || m_candidateCount == 0) {
    return;
  }

  size_t bestIndex = 0;
  for (size_t i = 1; i < m_candidateCount; ++i) {
    if (m_candidates[i].rssi > m_candidates[bestIndex].rssi) {
      bestIndex = i;
    }
  }

  BLEClient *client = BLEDevice::createClient();
  ScaleDevice *scale = nullptr;

  for (const auto &factory : kFactories) {
    if (factory.type == m_candidates[bestIndex].type) {
      scale = factory.create(m_candidates[bestIndex].address, m_candidates[bestIndex].addressType,
                             m_candidates[bestIndex].name, client);
      break;
    }
  }

  if (!scale) {
    delete client;
    return;
  }

  if (!scale->connect()) {
    scale->disconnect();
    delete scale;
    delete client;
    return;
  }

  m_client = client;
  m_activeScale = scale;
  m_lastKnown = m_candidates[bestIndex];
  m_hasLastKnown = true;
  m_lastKnownFromStorage = false;
  notifyLastKnownScaleChanged();
  stopScan();
}

void ScaleManager::tryConnectLastKnown(uint32_t nowMs) {
  if (!m_hasLastKnown || m_activeScale) {
    return;
  }
  const uint32_t staleMs = 15000;
  if (!m_lastKnownFromStorage && nowMs - m_lastKnown.lastSeenMs > staleMs) {
    return;
  }
  if (nowMs - m_lastConnectAttemptMs < 5000) {
    return;
  }
  m_lastConnectAttemptMs = nowMs;

  BLEClient *client = BLEDevice::createClient();
  ScaleDevice *scale = nullptr;

  for (const auto &factory : kFactories) {
    if (factory.type == m_lastKnown.type) {
      scale = factory.create(m_lastKnown.address, m_lastKnown.addressType, m_lastKnown.name, client);
      break;
    }
  }

  if (!scale) {
    delete client;
    return;
  }

  if (!scale->connect()) {
    scale->disconnect();
    delete scale;
    delete client;
    return;
  }

  m_client = client;
  m_activeScale = scale;
  notifyLastKnownScaleChanged();
  stopScan();
}

void ScaleManager::clearActiveScale() {
  if (m_activeScale) {
    m_activeScale->disconnect();
    delete m_activeScale;
    m_activeScale = nullptr;
  }
  if (m_client) {
    delete m_client;
    m_client = nullptr;
  }
}

void ScaleManager::notifyLastKnownScaleChanged() {
  if (!m_lastKnownScaleChangedCallback) {
    return;
  }

  SavedScale savedScale;
  if (getLastKnownScale(savedScale)) {
    m_lastKnownScaleChangedCallback(savedScale);
  }
}
