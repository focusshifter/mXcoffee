#pragma once

#include "ScaleDevice.h"
#include "ScaleTypes.h"
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <string>

namespace mxcoffee::scale {

struct ScaleDriver {
  ScaleType type;
  const char *name;
  bool (*matches)(BLEAdvertisedDevice &device);
  ScaleDevice *(*create)(const BLEAddress &address, esp_ble_addr_type_t addrType,
                         const std::string &name, BLEClient *client);
};

} // namespace mxcoffee::scale
