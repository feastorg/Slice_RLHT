#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include "config.h"

// ----- General BREAD -----
#define ESTOP 2
#define LED_PIN 5

// ----- Timing constants -----
#define SERIAL_UPDATE_TIME_MS 1000
#define THERMO_UPDATE_TIME_MS 300
// relayControlLogic() takes its masked snapshot of the command fields on each
// new reading and otherwise at most this often. A masked window on every loop
// pass delayed the TWI interrupt often enough to cut GET_STATE replies short
// (feastorg/Slice_RLHT#81). A command waits at most this long to drive a
// relay; the e-stop, watchdog and fault hold still act on every pass.
#ifndef RLHT_SNAPSHOT_INTERVAL_MS
#define RLHT_SNAPSHOT_INTERVAL_MS 10
#endif

// Gen1 has one relay, on D6; D7 is not connected (docs/hardware-revisions.md).
#if (RLHT_HW_GEN == 1)
#define RLHT_HAS_STATUS_LED 0
#define RLHT_RELAY_COUNT 1
#elif (RLHT_HW_GEN == 2)
#define RLHT_HAS_STATUS_LED 1
#define RLHT_RELAY_COUNT 2
#else
#error "Unsupported RLHT_HW_GEN value"
#endif

// ----- RLHT Specific -----
#define TC_CS1 10
#define TC_CS2 11
#define TC_DATA 12
#define TC_CLK 13

#define RELAY1 6
#if (RLHT_RELAY_COUNT >= 2)
#define RELAY2 7
#endif

#endif // HARDWARE_CONFIG_H
