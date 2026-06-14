#include "LfSmartScaleProtocol.h"

namespace mxcoffee::scale {

bool decodeLfSmartScaleWeight(const uint8_t *data, size_t length, float &weight) {
  if (!data || length < 6) {
    return false;
  }

  int16_t raw = static_cast<int16_t>((data[4] << 8) | data[3]);
  weight = static_cast<float>(raw) / 10.0f;
  if (data[5] > 0) {
    weight *= -1.0f;
  }
  return true;
}

} // namespace mxcoffee::scale
