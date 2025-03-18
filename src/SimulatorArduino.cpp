#include "SimulatorArduino.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef SIMULATOR
int Serial_begin(uint32_t baud) { return 0; }
int Serial_print(const char *str) { return printf("%s", str); }
int Serial_println(const char *str) { return printf("%s\n", str); }
int Serial_printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  int ret = vprintf(format, args);
  va_end(args);
  return ret;
}

unsigned long millis() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}
#endif

#ifdef __cplusplus
}
#endif