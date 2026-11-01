#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <bread_slice_core.h>
#include <crumbs.h>
#include <PID_v1.h>
#include <max6675.h>
#include <bread/rlht_ops.h>

#include "config_hardware.h"

enum ControlMode : uint8_t
{
    CLOSED_LOOP = RLHT_MODE_CLOSED_LOOP,
    OPEN_LOOP = RLHT_MODE_OPEN_LOOP
};

struct RelayHeater
{
    double setpointTemperature = 0.0;
    uint8_t thermocoupleSelect = 0;
    double relayOnTime = 0.0;
    uint16_t relayPeriod = 1000;
    double Kp = 1.0;
    double Ki = 0.0;
    double Kd = 0.0;
};

struct RLHT_SLICE
{
    double temperature1 = NAN;
    double temperature2 = NAN;
    RelayHeater relayHeater1;
    RelayHeater relayHeater2;
    ControlMode mode = CLOSED_LOOP;
    bool relay1State = false;
    bool relay2State = false;
};

struct Timing
{
    unsigned long lastThermoRead;
    unsigned long lastSerialPrint;
    unsigned long relay1Start;
    unsigned long relay2Start;
};

extern RLHT_SLICE slice;
extern Timing timing;
extern PID relay1PID;
extern PID relay2PID;

// Command watchdog (BREAD_OP_SET/GET/CLEAR_WATCHDOG*): boots disarmed
// (timeout 0) unless RLHT_WATCHDOG_BOOT_MS is defined. A trip latches until
// CLEAR_WATCHDOG_TRIP, the serial WDCLEAR command, or a reboot; mode,
// setpoint and duty commands are ignored while it is set. From
// bread-slice-core: arm(), stamp() and clear() are ISR-safe.
extern bsc::CommandWatchdog commandWatchdog;
// The lease (RLHT_CAP_WATCHDOG_LEASE): set by a 3-byte SET_WATCHDOG with
// BREAD_WATCHDOG_FLAG_COMMANDS_ONLY, cleared by the 2-byte form and serial
// WDOG=. While set, only SET_MODE, SET_SETPOINTS, SET_OPEN_DUTY and
// SET_WATCHDOG refresh the watchdog.
extern volatile bool watchdogCommandsOnly;
// Refreshes the watchdog for a reply, a non-actuating frame or a serial line:
// unless the lease is commands-only.
void livenessStamp();
// The e-stop input (pin ESTOP): asserted() and count(), the latter for
// RLHT_CAP_ESTOP_COUNT, are what the I2C handlers read.
extern bsc::EStop estopInput;
// Latched heater faults, RLHT_FAULT_* bits (#48). Set by the main loop,
// cleared by CLEAR_FAULTS or serial FAULTCLEAR; nonzero holds both heaters
// off and refuses mode, setpoint and duty commands.
extern volatile uint8_t heaterFaults;

void setupSlice();
void setupRLHT();
void updateStatusLed();
void processEStop(bsc::EStop::Event e);
void watchdogLogic();
bool measureThermocouples();
// The last reading of the thermocouple a select names: 2 is thermocouple 2,
// anything else thermocouple 1.
double selectedTemperature(uint8_t thermocoupleSelect);
// True while a heater's PID runs from its thermocouple: closed loop with a
// setpoint. Its select can't change then (feastorg/Slice_RLHT#54): the swap
// reads as a step in the measurement, and PID_v1 can't re-initialise without
// folding the P term into the integral. Call with interrupts masked, or from
// the TWI ISR.
bool thermocoupleSelectLocked(const RelayHeater &heater);
// Set where a select change is stored, cleared by relayControlLogic(), which
// restarts that heater's PID from 0 on the new sensor. A heater given a
// setpoint before the next pass would otherwise compute its first sample
// across the two sensors.
extern volatile bool tcSelectChanged1;
extern volatile bool tcSelectChanged2;
void checkHeaterFaults();
void relayControlLogic();
void setRelayPeriod(uint8_t relayId, uint16_t periodMs);
void actuateRelay(uint8_t relayPin, unsigned long &relayStart, unsigned long relayPeriod, unsigned long relayOnTime, bool &relayState);
void serialCommands();
void printSliceState(Print &out);
void printSerialOutput();

void handler_set_mode(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data);
void handler_set_setpoints(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data);
void handler_set_pid(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data);
void handler_set_periods(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data);
void handler_set_tc_select(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data);
void handler_set_open_duty(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data);
void handler_set_watchdog(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data);
void handler_clear_watchdog_trip(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data);
void handler_clear_faults(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data);
void reply_version(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data);
void reply_get_state(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data);
void reply_get_caps(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data);
void reply_get_watchdog(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data);
void reply_get_faults(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data);

#endif // GLOBALS_H
