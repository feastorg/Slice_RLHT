// Host stand-in for the Adafruit MAX6675 driver: readCelsius() returns the
// temperature a test set for that chip-select pin (host.cpp), NAN for an
// open thermocouple.
#pragma once
#include <stdint.h>

float host_thermocouple_celsius(int8_t cs);

class MAX6675
{
public:
    MAX6675(int8_t, int8_t cs, int8_t) : cs_(cs) {}
    float readCelsius() { return host_thermocouple_celsius(cs_); }

private:
    int8_t cs_;
};
