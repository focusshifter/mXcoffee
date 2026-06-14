#pragma once

#include <BLEClient.h>
#include <BLEAdvertisedDevice.h>
#include <string>

class ScaleDevice {
public:
  virtual ~ScaleDevice() = default;

  virtual bool connect() = 0;
  virtual void disconnect() = 0;
  virtual bool isConnected() const = 0;
  virtual float getWeight() const = 0;
  virtual const char *getName() const = 0;
};
