#include "BleProtocol.h"

#include <algorithm>

namespace mxcoffee::ble {

PressurePayload encodePressure(int16_t rawPressure, int16_t zeroOffset) {
  const int16_t adjustedPressure = static_cast<int16_t>(rawPressure - zeroOffset);
  const uint16_t encoded = static_cast<uint16_t>(adjustedPressure);
  return {static_cast<uint8_t>((encoded >> 8) & 0xff), static_cast<uint8_t>(encoded & 0xff)};
}

uint8_t clampBatteryLevel(int level) {
  return static_cast<uint8_t>(std::max(0, std::min(100, level)));
}

std::string logPayload(const std::string &message) { return message; }

void PressureState::updatePressure(int16_t rawPressure) { m_lastPressure = rawPressure; }

void PressureState::zero() { m_zeroOffset = m_lastPressure; }

int16_t PressureState::lastPressure() const { return m_lastPressure; }

int16_t PressureState::zeroOffset() const { return m_zeroOffset; }

PressurePayload PressureState::encodedPressure() const {
  return encodePressure(m_lastPressure, m_zeroOffset);
}

} // namespace mxcoffee::ble
