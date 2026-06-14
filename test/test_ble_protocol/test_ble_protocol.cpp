#include <unity.h>

#include "ble/BleProtocol.h"

#include <cstring>

using mxcoffee::ble::PressureState;
using mxcoffee::ble::clampBatteryLevel;
using mxcoffee::ble::encodePressure;
using mxcoffee::ble::logPayload;

void test_encode_pressure_zero(void) {
  const auto payload = encodePressure(0, 0);
  TEST_ASSERT_EQUAL_UINT8(0x00, payload[0]);
  TEST_ASSERT_EQUAL_UINT8(0x00, payload[1]);
}

void test_encode_pressure_positive_big_endian(void) {
  const auto payload = encodePressure(9000, 0);
  TEST_ASSERT_EQUAL_UINT8(0x23, payload[0]);
  TEST_ASSERT_EQUAL_UINT8(0x28, payload[1]);
}

void test_encode_pressure_negative_big_endian(void) {
  const auto payload = encodePressure(-1000, 0);
  TEST_ASSERT_EQUAL_UINT8(0xfc, payload[0]);
  TEST_ASSERT_EQUAL_UINT8(0x18, payload[1]);
}

void test_pressure_state_zero_offsets_next_reading(void) {
  PressureState state;
  state.updatePressure(10123);
  state.zero();
  state.updatePressure(10200);

  const auto payload = state.encodedPressure();
  TEST_ASSERT_EQUAL_INT16(10123, state.zeroOffset());
  TEST_ASSERT_EQUAL_UINT8(0x00, payload[0]);
  TEST_ASSERT_EQUAL_UINT8(0x4d, payload[1]);
}

void test_battery_level_is_clamped(void) {
  TEST_ASSERT_EQUAL_UINT8(0, clampBatteryLevel(-1));
  TEST_ASSERT_EQUAL_UINT8(42, clampBatteryLevel(42));
  TEST_ASSERT_EQUAL_UINT8(100, clampBatteryLevel(101));
}

void test_ble_contract_constants_are_stable(void) {
  TEST_ASSERT_EQUAL_STRING("PRS-mXcoffee", mxcoffee::ble::kDeviceName);
  TEST_ASSERT_EQUAL_UINT16(0x180f, mxcoffee::ble::kBatteryServiceUuid);
  TEST_ASSERT_EQUAL_UINT16(0x2a19, mxcoffee::ble::kBatteryLevelCharacteristicUuid);
  TEST_ASSERT_EQUAL_STRING("873ae828-4c5a-4342-b539-9d900bf7ebd0", mxcoffee::ble::kLogServiceUuid);
  TEST_ASSERT_EQUAL_STRING("873ae829-4c5a-4342-b539-9d900bf7ebd0", mxcoffee::ble::kLogCharacteristicUuid);
  TEST_ASSERT_EQUAL_STRING("873ae82a-4c5a-4342-b539-9d900bf7ebd0", mxcoffee::ble::kPressureServiceUuid);
  TEST_ASSERT_EQUAL_STRING("873ae82b-4c5a-4342-b539-9d900bf7ebd0", mxcoffee::ble::kPressureCharacteristicUuid);
  TEST_ASSERT_EQUAL_STRING("873ae82c-4c5a-4342-b539-9d900bf7ebd0", mxcoffee::ble::kPressureZeroCharacteristicUuid);
}

void test_log_payload_preserves_exact_bytes(void) {
  const std::string input("a\0b", 3);
  const std::string payload = logPayload(input);

  TEST_ASSERT_EQUAL_UINT32(input.size(), payload.size());
  TEST_ASSERT_EQUAL(0, std::memcmp(input.data(), payload.data(), input.size()));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_encode_pressure_zero);
  RUN_TEST(test_encode_pressure_positive_big_endian);
  RUN_TEST(test_encode_pressure_negative_big_endian);
  RUN_TEST(test_pressure_state_zero_offsets_next_reading);
  RUN_TEST(test_battery_level_is_clamped);
  RUN_TEST(test_ble_contract_constants_are_stable);
  RUN_TEST(test_log_payload_preserves_exact_bytes);
  return UNITY_END();
}
