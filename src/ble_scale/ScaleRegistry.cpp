#include "ScaleRegistry.h"
#include "LfSmartScale.h"

namespace mxcoffee::scale {
namespace {

constexpr ScaleDriver kDrivers[] = {
    {ScaleType::LfSmartScale, "LF Smart Scale", LfSmartScaleDevice::matches,
     LfSmartScaleDevice::create},
};

} // namespace

const ScaleDriver *scaleDrivers() {
  return kDrivers;
}

size_t scaleDriverCount() {
  return sizeof(kDrivers) / sizeof(kDrivers[0]);
}

const ScaleDriver *findScaleDriver(ScaleType type) {
  for (size_t i = 0; i < scaleDriverCount(); ++i) {
    if (kDrivers[i].type == type) {
      return &kDrivers[i];
    }
  }
  return nullptr;
}

const ScaleDriver *matchScaleDriver(BLEAdvertisedDevice &device) {
  for (size_t i = 0; i < scaleDriverCount(); ++i) {
    if (kDrivers[i].matches(device)) {
      return &kDrivers[i];
    }
  }
  return nullptr;
}

} // namespace mxcoffee::scale
