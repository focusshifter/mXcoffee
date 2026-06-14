#pragma once

#include <Arduino.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <stddef.h>
#include <stdint.h>

class Settings {
public:
  struct LastScaleConfig {
    char address[18] = {0};
    char name[32] = {0};
    uint8_t addressType = 0;
    uint8_t scaleType = 0;
  };

  Settings();
  ~Settings();

  bool begin();

  bool getBluetoothEnabled();
  void setBluetoothEnabled(bool enabled);

  bool saveScaleConfig(const char *json);
  bool loadScaleConfig(char *buffer, size_t bufferSize);

  bool saveLastScale(const LastScaleConfig &config);
  bool loadLastScale(LastScaleConfig &config);
  void clearLastScale();

  void reset();

private:
  bool nvsInitialized;
  nvs_handle_t nvsHandle;

  static const char *kNvsNamespace;
  static const char *kKeyBluetoothEnabled;
  static const char *kKeyLastScaleAddress;
  static const char *kKeyLastScaleAddressType;
  static const char *kKeyLastScaleType;
  static const char *kKeyLastScaleName;
};
