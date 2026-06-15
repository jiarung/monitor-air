#pragma once

#include <Arduino.h>

// One snapshot of all sensor values, returned fresh per read (no shared mutable
// state). A failed/garbage read leaves the matching `*Valid` flag false so the
// publisher can omit that field entirely.
struct SensorReading {
    float temp     = NAN;  // degC
    float hum      = NAN;  // %RH
    float pressure = NAN;  // hPa
    float gas      = NAN;  // kOhm
    float lux      = NAN;  // lux

    bool tempValid     = false;
    bool humValid      = false;
    bool pressureValid = false;
    bool gasValid      = false;
    bool luxValid      = false;
};

// Initialise the I2C bus and both sensors. Returns true if at least one sensor
// came up; never blocks or halts on failure (a network device must keep running).
bool sensorsBegin();

// Take a fresh reading. Returns a new value each call (no shared mutable state).
SensorReading sensorsRead();
