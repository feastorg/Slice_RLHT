#ifndef CONFIG_H
#define CONFIG_H

// Hardware generation has no default: it selects the relay pin map, and a
// wrong guess drives pins the board does not have (#27).
#ifndef RLHT_HW_GEN
#error "RLHT_HW_GEN is not set: build a gen* env from firmware/platformio.ini, e.g. pio run -e gen2_nano"
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
