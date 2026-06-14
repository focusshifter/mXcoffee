#include "Settings.h"
#include <FS.h>
#include <SPIFFS.h>
#include <string.h>

const char *Settings::kNvsNamespace = "mxcoffee";
const char *Settings::kKeyBluetoothEnabled = "bt_enabled";
const char *Settings::kKeyLastScaleAddress = "scale_addr";
const char *Settings::kKeyLastScaleAddressType = "scale_addr_typ";
const char *Settings::kKeyLastScaleType = "scale_type";
const char *Settings::kKeyLastScaleName = "scale_name";

Settings::Settings()
    : nvsInitialized(false), nvsHandle(0) {
}

Settings::~Settings() {
  if (nvsInitialized) {
    nvs_close(nvsHandle);
  }
}

bool Settings::begin() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }

  if (err != ESP_OK) {
    Serial.printf("Settings: NVS init failed: %s\n", esp_err_to_name(err));
    return false;
  }

  err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvsHandle);
  if (err != ESP_OK) {
    Serial.printf("Settings: Failed to open NVS namespace: %s\n", esp_err_to_name(err));
    return false;
  }

  nvsInitialized = true;

  if (!SPIFFS.begin(true)) {
    Serial.println("Settings: SPIFFS mount failed");
  } else {
    Serial.println("Settings: SPIFFS mounted successfully");
  }

  return true;
}

bool Settings::getBluetoothEnabled() {
  if (!nvsInitialized) {
    return false;
  }

  uint8_t enabled = 0;
  esp_err_t err = nvs_get_u8(nvsHandle, kKeyBluetoothEnabled, &enabled);
  if (err == ESP_OK) {
    return enabled != 0;
  }
  return false;
}

void Settings::setBluetoothEnabled(bool enabled) {
  if (!nvsInitialized) {
    Serial.println("Settings: NVS not initialized, cannot save bluetooth state");
    return;
  }

  esp_err_t err = nvs_set_u8(nvsHandle, kKeyBluetoothEnabled, enabled ? 1 : 0);
  if (err != ESP_OK) {
    Serial.printf("Settings: Failed to save bluetooth state: %s\n", esp_err_to_name(err));
    return;
  }

  err = nvs_commit(nvsHandle);
  if (err != ESP_OK) {
    Serial.printf("Settings: Failed to commit NVS: %s\n", esp_err_to_name(err));
    return;
  }

  Serial.printf("Settings: Saved bluetoothEnabled = %s\n", enabled ? "true" : "false");
}

bool Settings::saveScaleConfig(const char *json) {
  if (!SPIFFS.exists("/settings")) {
    SPIFFS.mkdir("/settings");
  }

  File file = SPIFFS.open("/settings/scales.json", "w");
  if (!file) {
    Serial.println("Settings: Failed to open scales.json for writing");
    return false;
  }

  size_t written = file.print(json);
  file.close();

  Serial.printf("Settings: Saved scale config (%zu bytes)\n", written);
  return written > 0;
}

bool Settings::loadScaleConfig(char *buffer, size_t bufferSize) {
  File file = SPIFFS.open("/settings/scales.json", "r");
  if (!file) {
    return false;
  }

  size_t bytesRead = file.readBytes(buffer, bufferSize - 1);
  buffer[bytesRead] = '\0';
  file.close();

  Serial.printf("Settings: Loaded scale config (%zu bytes)\n", bytesRead);
  return bytesRead > 0;
}

bool Settings::saveLastScale(const LastScaleConfig &config) {
  if (!nvsInitialized) {
    Serial.println("Settings: NVS not initialized, cannot save last scale");
    return false;
  }
  if (config.address[0] == '\0' || config.scaleType == 0) {
    Serial.println("Settings: Last scale config is incomplete");
    return false;
  }

  esp_err_t err = nvs_set_str(nvsHandle, kKeyLastScaleAddress, config.address);
  if (err == ESP_OK) {
    err = nvs_set_u8(nvsHandle, kKeyLastScaleAddressType, config.addressType);
  }
  if (err == ESP_OK) {
    err = nvs_set_u8(nvsHandle, kKeyLastScaleType, config.scaleType);
  }
  if (err == ESP_OK) {
    err = nvs_set_str(nvsHandle, kKeyLastScaleName, config.name);
  }
  if (err == ESP_OK) {
    err = nvs_commit(nvsHandle);
  }
  if (err != ESP_OK) {
    Serial.printf("Settings: Failed to save last scale: %s\n", esp_err_to_name(err));
    return false;
  }

  Serial.printf("Settings: Saved last scale %s (%s)\n", config.name, config.address);
  return true;
}

bool Settings::loadLastScale(LastScaleConfig &config) {
  if (!nvsInitialized) {
    return false;
  }

  LastScaleConfig loaded;
  size_t addressLength = sizeof(loaded.address);
  esp_err_t err = nvs_get_str(nvsHandle, kKeyLastScaleAddress, loaded.address, &addressLength);
  if (err != ESP_OK || loaded.address[0] == '\0') {
    return false;
  }

  size_t nameLength = sizeof(loaded.name);
  err = nvs_get_str(nvsHandle, kKeyLastScaleName, loaded.name, &nameLength);
  if (err != ESP_OK) {
    strncpy(loaded.name, "Unknown", sizeof(loaded.name) - 1);
    loaded.name[sizeof(loaded.name) - 1] = '\0';
  }

  err = nvs_get_u8(nvsHandle, kKeyLastScaleAddressType, &loaded.addressType);
  if (err != ESP_OK) {
    return false;
  }

  err = nvs_get_u8(nvsHandle, kKeyLastScaleType, &loaded.scaleType);
  if (err != ESP_OK || loaded.scaleType == 0) {
    return false;
  }

  config = loaded;
  Serial.printf("Settings: Loaded last scale %s (%s)\n", config.name, config.address);
  return true;
}

void Settings::clearLastScale() {
  if (!nvsInitialized) {
    return;
  }

  nvs_erase_key(nvsHandle, kKeyLastScaleAddress);
  nvs_erase_key(nvsHandle, kKeyLastScaleAddressType);
  nvs_erase_key(nvsHandle, kKeyLastScaleType);
  nvs_erase_key(nvsHandle, kKeyLastScaleName);
  nvs_commit(nvsHandle);
}

void Settings::reset() {
  if (!nvsInitialized) {
    return;
  }

  nvs_erase_all(nvsHandle);
  nvs_commit(nvsHandle);

  if (SPIFFS.exists("/settings/scales.json")) {
    SPIFFS.remove("/settings/scales.json");
  }

  Serial.println("Settings: All settings reset to defaults");
}
