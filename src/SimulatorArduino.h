#ifndef SIMULATOR_ARDUINO_H
#define SIMULATOR_ARDUINO_H

#include <math.h>
#include <stdint.h>

#ifdef SIMULATOR
// Mock Arduino types and functions for simulator
typedef uint8_t byte;
typedef uint16_t word;

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define PI M_PI

#ifdef __cplusplus
extern "C" {
#endif

// Mock Serial
extern int Serial_begin(uint32_t baud);
extern int Serial_print(const char *str);
extern int Serial_println(const char *str);
extern int Serial_printf(const char *format, ...);

unsigned long millis(void);

#ifdef __cplusplus
}
#endif
#endif

#endif