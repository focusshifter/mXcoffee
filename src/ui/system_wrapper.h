#ifndef SYSTEM_WRAPPER_H
#define SYSTEM_WRAPPER_H

class SystemWrapper {
public:
  virtual int16_t displayWidth() = 0;
  virtual int16_t displayHeight() = 0;
  virtual int32_t getBatteryLevel() = 0;
};

extern SystemWrapper *sysWrapper; // Declaration only

#endif