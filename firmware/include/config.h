#ifndef CONFIG_H
#define CONFIG_H

// Hardware generation has no default: it selects the relay pin map, and a
// wrong guess drives pins the board does not have (#27).
#ifndef RLHT_HW_GEN
#error "RLHT_HW_GEN is not set: build a gen* env from firmware/platformio.ini, pio run -e <env>, with the env for the board (docs/hardware-revisions.md)"
#endif

// Bus address is independent of hardware generation.
// Override with a build flag: -DI2C_ADR=<addr>
// 19 (0x13) is outside the addresses CRUMBS says to avoid (#12).
#ifndef I2C_ADR
#define I2C_ADR 19
#endif

// Highest setpoint, in whole degrees C, that SET_SETPOINTS and serial
// R1TEMP=/R2TEMP= accept (#47). Above it, SET_SETPOINTS is refused
// UNDEFINED_VALUE and serial replies out of range; nothing is stored.
// Override with a build flag: -DRLHT_SETPOINT_MAX_C=<C>
#ifndef RLHT_SETPOINT_MAX_C
#define RLHT_SETPOINT_MAX_C 500
#endif

// Heater faults (#48, RLHT_CAP_FAULTS). Each latches until CLEAR_FAULTS,
// serial FAULTCLEAR or a reboot, and holds both heaters off meanwhile.
// These are starting values, not measured ones: a deployment sets them for
// its equipment with build flags.
//
// Over-temperature, in either mode: RLHT_OVERTEMP_READS consecutive
// readings of the heater's thermocouple above RLHT_OVERTEMP_C (whole degrees
// C). One bad reading does not latch it (#70).
#ifndef RLHT_OVERTEMP_C
#define RLHT_OVERTEMP_C 525
#endif
#ifndef RLHT_OVERTEMP_READS
#define RLHT_OVERTEMP_READS 3
#endif
// No rise, in closed loop: the PID output has stayed at or above
// RLHT_NO_RISE_OUTPUT_PCT of the relay period for RLHT_NO_RISE_MS, and the
// temperature has risen less than RLHT_NO_RISE_DELTA_C in that time. The
// window has to be longer than the slowest healthy heat-up from cold at full
// output. This catches a probe that has come off the heater or is shorted.
#ifndef RLHT_NO_RISE_MS
#define RLHT_NO_RISE_MS 120000UL
#endif
#ifndef RLHT_NO_RISE_OUTPUT_PCT
#define RLHT_NO_RISE_OUTPUT_PCT 90
#endif
#ifndef RLHT_NO_RISE_DELTA_C
#define RLHT_NO_RISE_DELTA_C 2.0
#endif
// Sensor, in closed loop with a setpoint: this many consecutive readings of
// an open thermocouple (NaN). One reading every THERMO_UPDATE_TIME_MS; the
// first readings after a reset can be NaN.
#ifndef RLHT_SENSOR_FAULT_READS
#define RLHT_SENSOR_FAULT_READS 3
#endif

#ifdef SLICE_DEBUG

#define SLICE_DEBUG_PRINT(...) \
    Serial.print(__VA_ARGS__)

#define SLICE_DEBUG_PRINTLN(...) \
    Serial.println(__VA_ARGS__)

#else
#define SLICE_DEBUG_PRINT(...)
#define SLICE_DEBUG_PRINTLN(...)
#endif

#endif // CONFIG_H
