#ifndef CONFIG_H
#define CONFIG_H

// Hardware generation has no default: it selects the relay pin map, and a
// wrong guess drives pins the board does not have (#27).
#ifndef RLHT_HW_GEN
#error "RLHT_HW_GEN is not set: build a gen* env from firmware/platformio.ini, e.g. pio run -e gen2_nano"
#endif

// Bus address is independent of hardware generation.
// Override with a build flag: -DI2C_ADR=<addr>
#ifndef I2C_ADR
#define I2C_ADR 10
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
