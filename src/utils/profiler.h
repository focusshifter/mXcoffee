#include <Arduino.h>

class Profiler {
private:
  const char* name;
  unsigned long startTime;

public:
  Profiler(const char* functionName) {
    name = functionName;
    startTime = micros();
    Serial.printf("Start: %s\n", name);
  }
  
  ~Profiler() {
    unsigned long duration = micros() - startTime;
    Serial.printf("End: %s took %lu us\n", name, duration);
  }
};

