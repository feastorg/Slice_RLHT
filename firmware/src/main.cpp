#include <Arduino.h>
#include <FastLED.h>
#include <PID_v1.h>
#include <max6675.h>

#include <crumbs.h>
#include <crumbs_arduino.h>
#include <bread/rlht_ops.h>

// Watchdog
#if defined(__AVR__) || defined(ARDUINO_ARCH_MEGAAVR)
#include <avr/wdt.h>
#endif

#include "config.h"
#include "config_hardware.h"
#include "globals.h"

static crumbs_context_t ctx;
volatile bool estopTriggered = false;
CRGB led;

static const unsigned long ESTOP_DEBOUNCE_MS = 25;
static bool estopDebouncePending = false;
static unsigned long estopDebounceStartMs = 0;

RLHT_SLICE slice;
Timing timing = {0};

static ControlMode appliedMode = CLOSED_LOOP;
static bool pidTuningsApplied = false;
static double lastKp1 = 0.0;
static double lastKi1 = 0.0;
static double lastKd1 = 0.0;
static double lastKp2 = 0.0;
static double lastKi2 = 0.0;
static double lastKd2 = 0.0;

static uint16_t clampRelayPeriod(uint16_t p)
{
    if (p < 100u)
        return 100u;
    if (p > 10000u)
        return 10000u;
    return p;
}

// Deferred relay period storage: ISR writes pending values, main loop applies.
static volatile uint16_t pendingPeriod1 = 0;
static volatile uint16_t pendingPeriod2 = 0;
static volatile bool periodPending1 = false;
static volatile bool periodPending2 = false;

MAX6675 thermocouple1(TC_CLK, TC_CS1, TC_DATA);
MAX6675 thermocouple2(TC_CLK, TC_CS2, TC_DATA);

PID relay1PID(&(slice.relayHeater1.inputTemperature), &(slice.relayHeater1.relayOnTime), &(slice.relayHeater1.setpointTemperature),
              slice.relayHeater1.Kp, slice.relayHeater1.Ki, slice.relayHeater1.Kd, DIRECT);
PID relay2PID(&(slice.relayHeater2.inputTemperature), &(slice.relayHeater2.relayOnTime), &(slice.relayHeater2.setpointTemperature),
              slice.relayHeater2.Kp, slice.relayHeater2.Ki, slice.relayHeater2.Kd, DIRECT);

static void apply_mode_transition_if_needed()
{
    if (slice.mode == appliedMode)
        return;

    // PID_v1 calls Initialize() on MANUAL->AUTOMATIC transitions.
    // Apply mode only on transitions to avoid repeated integral re-seeding.
    if (slice.mode == CLOSED_LOOP)
    {
        relay1PID.SetMode(AUTOMATIC);
        relay2PID.SetMode(AUTOMATIC);
        appliedMode = CLOSED_LOOP;
        return;
    }

    if (slice.mode == OPEN_LOOP)
    {
        relay1PID.SetMode(MANUAL);
        relay2PID.SetMode(MANUAL);
        appliedMode = OPEN_LOOP;
    }
}

static void apply_tunings_if_needed()
{
    bool changed = !pidTuningsApplied ||
                   slice.relayHeater1.Kp != lastKp1 ||
                   slice.relayHeater1.Ki != lastKi1 ||
                   slice.relayHeater1.Kd != lastKd1 ||
                   slice.relayHeater2.Kp != lastKp2 ||
                   slice.relayHeater2.Ki != lastKi2 ||
                   slice.relayHeater2.Kd != lastKd2;

    if (!changed)
        return;

    relay1PID.SetTunings(slice.relayHeater1.Kp, slice.relayHeater1.Ki, slice.relayHeater1.Kd);
    relay2PID.SetTunings(slice.relayHeater2.Kp, slice.relayHeater2.Ki, slice.relayHeater2.Kd);

    lastKp1 = slice.relayHeater1.Kp;
    lastKi1 = slice.relayHeater1.Ki;
    lastKd1 = slice.relayHeater1.Kd;
    lastKp2 = slice.relayHeater2.Kp;
    lastKi2 = slice.relayHeater2.Ki;
    lastKd2 = slice.relayHeater2.Kd;
    pidTuningsApplied = true;
}

void setRelayPeriod(uint8_t relayId, uint16_t periodMs)
{
    uint16_t p = clampRelayPeriod(periodMs);

    if (relayId == 1)
    {
        pendingPeriod1 = p;
        periodPending1 = true;
        return;
    }

    if (relayId == 2)
    {
        pendingPeriod2 = p;
        periodPending2 = true;
    }
}

// Apply deferred relay period changes from main loop context (PID not ISR-safe).
static void applyPendingPeriods()
{
    if (periodPending1)
    {
        noInterrupts();
        uint16_t p = pendingPeriod1;
        periodPending1 = false;
        interrupts();

        slice.relayHeater1.relayPeriod = p;
        if (slice.relayHeater1.relayOnTime > (double)p)
            slice.relayHeater1.relayOnTime = (double)p;
        relay1PID.SetOutputLimits(0, p);
    }

    if (periodPending2)
    {
        noInterrupts();
        uint16_t p = pendingPeriod2;
        periodPending2 = false;
        interrupts();

        slice.relayHeater2.relayPeriod = p;
        if (slice.relayHeater2.relayOnTime > (double)p)
            slice.relayHeater2.relayOnTime = (double)p;
        relay2PID.SetOutputLimits(0, p);
    }
}

void setup()
{
    wdt_disable();
    setupSlice();
    setupRLHT();
    delay(1000);
    wdt_enable(WDTO_1S);
}

void loop()
{
    wdt_reset();
    pollEStop();
    measureThermocouples();
    relayControlLogic();
    serialCommands();
    printSerialOutput();
}

void setupSlice()
{
    int rc;

    Serial.begin(115200);

    crumbs_arduino_init_peripheral(&ctx, I2C_ADR);

    rc = crumbs_register_handler(&ctx, RLHT_OP_SET_MODE, handler_set_mode, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register RLHT_OP_SET_MODE"));

    rc = crumbs_register_handler(&ctx, RLHT_OP_SET_SETPOINTS, handler_set_setpoints, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register RLHT_OP_SET_SETPOINTS"));

    rc = crumbs_register_handler(&ctx, RLHT_OP_SET_PID, handler_set_pid, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register RLHT_OP_SET_PID"));

    rc = crumbs_register_handler(&ctx, RLHT_OP_SET_PERIODS, handler_set_periods, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register RLHT_OP_SET_PERIODS"));

    rc = crumbs_register_handler(&ctx, RLHT_OP_SET_TC_SELECT, handler_set_tc_select, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register RLHT_OP_SET_TC_SELECT"));

    rc = crumbs_register_handler(&ctx, RLHT_OP_SET_OPEN_DUTY, handler_set_open_duty, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register RLHT_OP_SET_OPEN_DUTY"));

    rc = crumbs_register_reply_handler(&ctx, 0x00, reply_version, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register version reply handler"));

    rc = crumbs_register_reply_handler(&ctx, RLHT_OP_GET_STATE, reply_get_state, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register RLHT_OP_GET_STATE reply handler"));

    rc = crumbs_register_reply_handler(&ctx, BREAD_OP_GET_CAPS, reply_get_caps, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register BREAD_OP_GET_CAPS reply handler"));

#if RLHT_HAS_STATUS_LED
    FastLED.addLeds<NEOPIXEL, LED_PIN>(&led, 1);
    FastLED.setBrightness(50);
    led = CRGB::Blue;
    FastLED.show();
#endif

    // e-stop: no external bias resistor on current boards, so use internal pull-up.
    pinMode(ESTOP, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(ESTOP), estopISR, CHANGE);
    estopTriggered = true; // Force initial debounced state sync after boot.

    SLICE_DEBUG_PRINTLN(F("RLHT SLICE INITIALIZED"));
    SLICE_DEBUG_PRINT(F("VERSION: "));
    SLICE_DEBUG_PRINT(RLHT_MODULE_VER_MAJOR);
    SLICE_DEBUG_PRINT(F("."));
    SLICE_DEBUG_PRINT(RLHT_MODULE_VER_MINOR);
    SLICE_DEBUG_PRINT(F("."));
    SLICE_DEBUG_PRINTLN(RLHT_MODULE_VER_PATCH);
#if (RLHT_HW_GEN == 1)
    SLICE_DEBUG_PRINTLN(F("HW Profile: Gen1"));
#else
    SLICE_DEBUG_PRINTLN(F("HW Profile: Gen2"));
#endif
}

void setupRLHT()
{
    pinMode(RELAY1, OUTPUT);
    pinMode(RELAY2, OUTPUT);

    digitalWrite(RELAY1, LOW);
    digitalWrite(RELAY2, LOW);

    // Deterministic default mapping: Relay1->TC1, Relay2->TC2.
    // Controllers may override this later via RLHT_OP_SET_TC_SELECT.
    slice.relayHeater1.thermocoupleSelect = 1;
    slice.relayHeater2.thermocoupleSelect = 2;

    // Apply initial relay periods directly (no ISR active during setup).
    slice.relayHeater1.relayPeriod = clampRelayPeriod(slice.relayHeater1.relayPeriod);
    relay1PID.SetOutputLimits(0, slice.relayHeater1.relayPeriod);
    slice.relayHeater2.relayPeriod = clampRelayPeriod(slice.relayHeater2.relayPeriod);
    relay2PID.SetOutputLimits(0, slice.relayHeater2.relayPeriod);

    relay1PID.SetMode(AUTOMATIC);
    relay2PID.SetMode(AUTOMATIC);
    appliedMode = CLOSED_LOOP;
    apply_tunings_if_needed();

    timing.lastThermoRead = millis();
    timing.lastSerialPrint = millis();
    timing.relay1Start = millis();
    timing.relay2Start = millis();
}

void pollEStop()
{
    if (estopTriggered)
    {
        estopTriggered = false;
        estopDebouncePending = true;
        estopDebounceStartMs = millis();
    }

    if (estopDebouncePending && (millis() - estopDebounceStartMs >= ESTOP_DEBOUNCE_MS))
    {
        processEStop();
        estopDebouncePending = false;
    }

#if RLHT_HAS_STATUS_LED
    {
        static CRGB lastLed = CRGB::Black;
        CRGB next = slice.eStop ? CRGB::Red : CRGB::Green;
        if (next != lastLed)
        {
            led = next;
            FastLED.show();
            lastLed = next;
        }
    }
#endif
}

void estopISR()
{
    estopTriggered = true;
}

void processEStop()
{
    // Internal pull-up means asserted e-stop pulls the line LOW.
    if (digitalRead(ESTOP) == LOW)
    {
        noInterrupts();
        slice.relayHeater1.setpointTemperature = 0;
        slice.relayHeater2.setpointTemperature = 0;
        slice.relayHeater1.relayOnTime = 0;
        slice.relayHeater2.relayOnTime = 0;
        slice.relay1State = false;
        slice.relay2State = false;
        slice.eStop = true;
        interrupts();

        digitalWrite(RELAY1, LOW);
        digitalWrite(RELAY2, LOW);

        SLICE_DEBUG_PRINTLN(F("ESTOP PRESSED!"));
    }
    else
    {
        noInterrupts();
        slice.eStop = false;
        interrupts();
        SLICE_DEBUG_PRINTLN(F("ESTOP RELEASED!"));
    }
}

void measureThermocouples()
{
    if (millis() - timing.lastThermoRead >= THERMO_UPDATE_TIME_MS)
    {
        slice.temperature1 = thermocouple1.readCelsius();
        slice.temperature2 = thermocouple2.readCelsius();
        timing.lastThermoRead += THERMO_UPDATE_TIME_MS;
    }
}

void relayControlLogic()
{
    // Apply any deferred relay period changes from ISR context.
    applyPendingPeriods();

    // Snapshot ISR-written command fields atomically.
    noInterrupts();
    bool localEStop = slice.eStop;
    ControlMode localMode = slice.mode;
    double sp1 = slice.relayHeater1.setpointTemperature;
    double sp2 = slice.relayHeater2.setpointTemperature;
    double kp1 = slice.relayHeater1.Kp;
    double ki1 = slice.relayHeater1.Ki;
    double kd1 = slice.relayHeater1.Kd;
    double kp2 = slice.relayHeater2.Kp;
    double ki2 = slice.relayHeater2.Ki;
    double kd2 = slice.relayHeater2.Kd;
    uint8_t tc1 = slice.relayHeater1.thermocoupleSelect;
    uint8_t tc2 = slice.relayHeater2.thermocoupleSelect;
    double onTime1 = slice.relayHeater1.relayOnTime;
    double onTime2 = slice.relayHeater2.relayOnTime;
    interrupts();

    // Apply snapshot to slice for PID (which uses pointers into slice).
    slice.relayHeater1.setpointTemperature = sp1;
    slice.relayHeater2.setpointTemperature = sp2;
    slice.relayHeater1.Kp = kp1;
    slice.relayHeater1.Ki = ki1;
    slice.relayHeater1.Kd = kd1;
    slice.relayHeater2.Kp = kp2;
    slice.relayHeater2.Ki = ki2;
    slice.relayHeater2.Kd = kd2;
    slice.relayHeater1.thermocoupleSelect = tc1;
    slice.relayHeater2.thermocoupleSelect = tc2;

    if (localEStop)
    {
        digitalWrite(RELAY1, LOW);
        digitalWrite(RELAY2, LOW);
        noInterrupts();
        slice.relay1State = false;
        slice.relay2State = false;
        interrupts();
        return;
    }

    slice.mode = localMode;
    apply_mode_transition_if_needed();
    apply_tunings_if_needed();

    if (localMode == CLOSED_LOOP)
    {
        switch (tc1)
        {
        case 1:
            slice.relayHeater1.inputTemperature = slice.temperature1;
            break;
        case 2:
            slice.relayHeater1.inputTemperature = slice.temperature2;
            break;
        default:
            break;
        }

        if (isnan(slice.relayHeater1.inputTemperature))
            slice.relayHeater1.relayOnTime = 0;
        else
            relay1PID.Compute();

        switch (tc2)
        {
        case 1:
            slice.relayHeater2.inputTemperature = slice.temperature1;
            break;
        case 2:
            slice.relayHeater2.inputTemperature = slice.temperature2;
            break;
        default:
            break;
        }

        if (isnan(slice.relayHeater2.inputTemperature))
            slice.relayHeater2.relayOnTime = 0;
        else
            relay2PID.Compute();
    }
    else if (localMode == OPEN_LOOP)
    {
        // In open-loop, use the ISR-provided on-times directly.
        slice.relayHeater1.relayOnTime = onTime1;
        slice.relayHeater2.relayOnTime = onTime2;
    }
    else
    {
        digitalWrite(RELAY1, LOW);
        digitalWrite(RELAY2, LOW);
        noInterrupts();
        slice.relay1State = false;
        slice.relay2State = false;
        interrupts();
        SLICE_DEBUG_PRINTLN(F("ERROR INVALID MODE, ENTERED UNKNOWN STATE!"));
        return;
    }

    actuateRelay(RELAY1, timing.relay1Start, (unsigned long)slice.relayHeater1.relayPeriod, (unsigned long)slice.relayHeater1.relayOnTime, slice.relay1State);
    actuateRelay(RELAY2, timing.relay2Start, (unsigned long)slice.relayHeater2.relayPeriod, (unsigned long)slice.relayHeater2.relayOnTime, slice.relay2State);

    // Commit output fields atomically for reply_get_state (called from ISR).
    noInterrupts();
    slice.relay1State = slice.relay1State;
    slice.relay2State = slice.relay2State;
    interrupts();
}

void actuateRelay(uint8_t relayPin, unsigned long &relayStart, unsigned long relayPeriod, unsigned long relayOnTime, bool &relayState)
{
    unsigned long currentTime = millis();

    if (relayPeriod == 0)
    {
        digitalWrite(relayPin, LOW);
        relayState = false;
        return;
    }

    if (currentTime - relayStart > relayPeriod)
    {
        relayStart += relayPeriod;
    }

    relayState = ((currentTime - relayStart) < relayOnTime);
    digitalWrite(relayPin, relayState ? HIGH : LOW);
}
