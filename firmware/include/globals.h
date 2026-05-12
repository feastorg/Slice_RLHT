#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <FastLED.h>
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
    double inputTemperature = 0.0;
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
    bool eStop = false;
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

extern volatile bool estopTriggered;
extern CRGB led;
extern RLHT_SLICE slice;
extern Timing timing;
extern PID relay1PID;
extern PID relay2PID;

void setupSlice();
void setupRLHT();
void pollEStop();
void estopISR();
void processEStop();
void measureThermocouples();
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
void reply_version(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data);
void reply_get_state(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data);
void reply_get_caps(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data);

#endif // GLOBALS_H
