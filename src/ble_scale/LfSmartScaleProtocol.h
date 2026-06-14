#pragma once

#include <cstddef>
#include <cstdint>

namespace mxcoffee::scale {

bool decodeLfSmartScaleWeight(const uint8_t *data, size_t length, float &weight);

} // namespace mxcoffee::scale
