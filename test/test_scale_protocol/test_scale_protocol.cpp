#include <unity.h>

#include "ble_scale/LfSmartScaleProtocol.h"
#include "ble_scale/ScaleTypes.h"

using mxcoffee::scale::ScaleType;
using mxcoffee::scale::decodeLfSmartScaleWeight;
using mxcoffee::scale::isKnownScaleType;
using mxcoffee::scale::scaleTypeName;

void test_lf_weight_payload_positive_decodes_tenths(void) {
  const uint8_t payload[] = {0x00, 0x00, 0x00, 0xd2, 0x04, 0x00};
  float weight = 0.0f;

  TEST_ASSERT_TRUE(decodeLfSmartScaleWeight(payload, sizeof(payload), weight));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 123.4f, weight);
}

void test_lf_weight_payload_negative_uses_sign_byte(void) {
  const uint8_t payload[] = {0x00, 0x00, 0x00, 0x2a, 0x00, 0x01};
  float weight = 0.0f;

  TEST_ASSERT_TRUE(decodeLfSmartScaleWeight(payload, sizeof(payload), weight));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -4.2f, weight);
}

void test_lf_weight_payload_rejects_short_packets(void) {
  const uint8_t payload[] = {0x00, 0x00, 0x00, 0x2a, 0x00};
  float weight = 99.0f;

  TEST_ASSERT_FALSE(decodeLfSmartScaleWeight(payload, sizeof(payload), weight));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 99.0f, weight);
}

void test_scale_type_ids_are_storage_stable(void) {
  TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(ScaleType::Unknown));
  TEST_ASSERT_EQUAL_UINT8(1, static_cast<uint8_t>(ScaleType::LfSmartScale));
  TEST_ASSERT_FALSE(isKnownScaleType(ScaleType::Unknown));
  TEST_ASSERT_TRUE(isKnownScaleType(ScaleType::LfSmartScale));
  TEST_ASSERT_EQUAL_STRING("LF Smart Scale", scaleTypeName(ScaleType::LfSmartScale));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_lf_weight_payload_positive_decodes_tenths);
  RUN_TEST(test_lf_weight_payload_negative_uses_sign_byte);
  RUN_TEST(test_lf_weight_payload_rejects_short_packets);
  RUN_TEST(test_scale_type_ids_are_storage_stable);
  return UNITY_END();
}
