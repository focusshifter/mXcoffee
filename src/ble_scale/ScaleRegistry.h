#pragma once

#include "ScaleDriver.h"
#include <cstddef>

namespace mxcoffee::scale {

const ScaleDriver *scaleDrivers();
size_t scaleDriverCount();
const ScaleDriver *findScaleDriver(ScaleType type);
const ScaleDriver *matchScaleDriver(BLEAdvertisedDevice &device);

} // namespace mxcoffee::scale
