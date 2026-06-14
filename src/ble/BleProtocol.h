#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace mxcoffee::ble {

using PressurePayload = std::array<uint8_t, 2>;

constexpr const char *kDeviceName = "PRS-mXcoffee";

constexpr uint16_t kBatteryServiceUuid = 0x180F;
constexpr uint16_t kBatteryLevelCharacteristicUuid = 0x2A19;

constexpr const char *kLogServiceUuid = "873ae828-4c5a-4342-b539-9d900bf7ebd0";
constexpr const char *kLogCharacteristicUuid = "873ae829-4c5a-4342-b539-9d900bf7ebd0";

constexpr const char *kPressureServiceUuid = "873ae82a-4c5a-4342-b539-9d900bf7ebd0";
constexpr const char *kPressureCharacteristicUuid = "873ae82b-4c5a-4342-b539-9d900bf7ebd0";
constexpr const char *kPressureZeroCharacteristicUuid = "873ae82c-4c5a-4342-b539-9d900bf7ebd0";

PressurePayload encodePressure(int16_t rawPressure, int16_t zeroOffset);
uint8_t clampBatteryLevel(int level);
std::string logPayload(const std::string &message);

class PressureState {
public:
  void updatePressure(int16_t rawPressure);
  void zero();

  int16_t lastPressure() const;
  int16_t zeroOffset() const;
  PressurePayload encodedPressure() const;

private:
  int16_t m_lastPressure = 0;
  int16_t m_zeroOffset = 0;
};

} // namespace mxcoffee::ble
