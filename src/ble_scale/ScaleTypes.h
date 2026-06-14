#pragma once

#include <cstdint>

namespace mxcoffee::scale {

enum class ScaleType : uint8_t {
  Unknown = 0,
  LfSmartScale = 1
};

const char *scaleTypeName(ScaleType type);
bool isKnownScaleType(ScaleType type);

} // namespace mxcoffee::scale
