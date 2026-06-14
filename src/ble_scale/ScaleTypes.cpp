#include "ScaleTypes.h"

namespace mxcoffee::scale {

const char *scaleTypeName(ScaleType type) {
  switch (type) {
  case ScaleType::LfSmartScale:
    return "LF Smart Scale";
  case ScaleType::Unknown:
  default:
    return "Unknown";
  }
}

bool isKnownScaleType(ScaleType type) {
  return type != ScaleType::Unknown;
}

} // namespace mxcoffee::scale
