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

// Command watchdog state (see globals.h). ISR-written; main loop snapshots
// under short masked windows.
volatile uint16_t wdTimeoutMs = 0;
volatile unsigned long wdLastRxMs = 0;
volatile bool wdTripped = false;
volatile uint8_t wdTripCount = 0;

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

static void relaysLow()
{
    digitalWrite(RELAY1, LOW);
#if (RLHT_RELAY_COUNT >= 2)
    digitalWrite(RELAY2, LOW);
#endif
}

// Deferred relay period storage: ISR writes pending values, main loop applies.
static volatile uint16_t pendingPeriod1 = 0;
static volatile uint16_t pendingPeriod2 = 0;
static volatile bool periodPending1 = false;
static volatile bool periodPending2 = false;

MAX6675 thermocouple1(TC_CLK, TC_CS1, TC_DATA);
MAX6675 thermocouple2(TC_CLK, TC_CS2, TC_DATA);

// The PIDs work on main-loop-owned copies, never on slice fields the command
// handlers write: writing a snapshot back into slice overwrote any command
// that landed between the snapshot and the write-back (feastorg/Slice_RLHT#14).
static double pidInput1 = 0.0;
static double pidOutput1 = 0.0;
static double pidSetpoint1 = 0.0;
static double pidInput2 = 0.0;
static double pidOutput2 = 0.0;
static double pidSetpoint2 = 0.0;

PID relay1PID(&pidInput1, &pidOutput1, &pidSetpoint1,
              slice.relayHeater1.Kp, slice.relayHeater1.Ki, slice.relayHeater1.Kd, DIRECT);
PID relay2PID(&pidInput2, &pidOutput2, &pidSetpoint2,
              slice.relayHeater2.Kp, slice.relayHeater2.Ki, slice.relayHeater2.Kd, DIRECT);

static void apply_mode_transition_if_needed(ControlMode mode)
{
    if (mode == appliedMode)
        return;

    if (mode == CLOSED_LOOP)
    {
        // Start from 0, not from the open-loop duty or an earlier PID output:
        // computeHeater() switches each PID to automatic with a fresh input,
        // and Initialize() seeds the integral from this output
        // (feastorg/Slice_RLHT#17).
        pidOutput1 = 0;
        pidOutput2 = 0;
        appliedMode = CLOSED_LOOP;
        return;
    }

    if (mode == OPEN_LOOP)
    {
        relay1PID.SetMode(MANUAL);
        relay2PID.SetMode(MANUAL);
        appliedMode = OPEN_LOOP;
    }
}

static void apply_tunings_if_needed(double kp1, double ki1, double kd1,
                                    double kp2, double ki2, double kd2)
{
    bool changed = !pidTuningsApplied ||
                   kp1 != lastKp1 || ki1 != lastKi1 || kd1 != lastKd1 ||
                   kp2 != lastKp2 || ki2 != lastKi2 || kd2 != lastKd2;

    if (!changed)
        return;

    relay1PID.SetTunings(kp1, ki1, kd1);
    relay2PID.SetTunings(kp2, ki2, kd2);

    lastKp1 = kp1;
    lastKi1 = ki1;
    lastKd1 = kd1;
    lastKp2 = kp2;
    lastKi2 = ki2;
    lastKd2 = kd2;
    pidTuningsApplied = true;
}

// A heater with no setpoint is off: its output is held at 0 with its PID in
// manual. PID_v1 re-initialises the integral from the output when it goes
// back to automatic, so a heater starts from 0 when a setpoint returns,
// instead of resuming the duty it had built up (feastorg/Slice_RLHT#13).
static void computeHeater(PID &pid, double &pidSetpoint, double &pidInput, double &pidOutput,
                          double setpoint, double input)
{
    if (setpoint <= 0)
    {
        pid.SetMode(MANUAL);
        pidOutput = 0;
        return;
    }
    if (isnan(input))
    {
        // Before AUTOMATIC: a re-initialisation from a NaN input would make
        // the next Compute() NaN.
        pidOutput = 0;
        return;
    }
    pidSetpoint = setpoint;
    pidInput = input;
    pid.SetMode(AUTOMATIC);
    pid.Compute();
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
    // The period and the on-time clamp go in one masked window: the open-duty
    // handler reads relayPeriod and writes relayOnTime from the ISR.
    if (periodPending1)
    {
        noInterrupts();
        uint16_t p = pendingPeriod1;
        periodPending1 = false;
        slice.relayHeater1.relayPeriod = p;
        if (slice.relayHeater1.relayOnTime > (double)p)
            slice.relayHeater1.relayOnTime = (double)p;
        interrupts();

        relay1PID.SetOutputLimits(0, p);
        if (pidOutput1 > (double)p)
            pidOutput1 = (double)p;
    }

    if (periodPending2)
    {
        noInterrupts();
        uint16_t p = pendingPeriod2;
        periodPending2 = false;
        slice.relayHeater2.relayPeriod = p;
        if (slice.relayHeater2.relayOnTime > (double)p)
            slice.relayHeater2.relayOnTime = (double)p;
        interrupts();

        relay2PID.SetOutputLimits(0, p);
        if (pidOutput2 > (double)p)
            pidOutput2 = (double)p;
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
    watchdogLogic();
    measureThermocouples();
    relayControlLogic();
    serialCommands();
    printSerialOutput();
}

// Fires for every CRC-valid inbound command frame (SET_REPLY excluded by
// CRUMBS): any valid command proves a live master and clears a trip.
static void on_crumbs_message(crumbs_context_t *c, const crumbs_message_t *msg)
{
    (void)c;
    (void)msg;
    wdLastRxMs = millis();
    wdTripped = false;
}

void watchdogLogic()
{
    uint16_t timeout;
    unsigned long lastRx;
    bool tripped;

    noInterrupts();
    timeout = wdTimeoutMs;
    lastRx = wdLastRxMs;
    tripped = wdTripped;
    interrupts();

    if (timeout == 0 || tripped)
        return;  // relayControlLogic holds relays LOW while tripped

    if (millis() - lastRx < timeout)
        return;

    relaysLow();

    // Same safe-state fields as processEStop, without touching eStop.
    noInterrupts();
    wdTripped = true;
    wdTripCount++;
    slice.relayHeater1.setpointTemperature = 0;
    slice.relayHeater2.setpointTemperature = 0;
    slice.relayHeater1.relayOnTime = 0;
    slice.relayHeater2.relayOnTime = 0;
    slice.relay1State = false;
    slice.relay2State = false;
    interrupts();
    // A command that clears the trip may bring a setpoint before the loop
    // ever sees this one at 0: drop the integral now.
    relay1PID.SetMode(MANUAL);
    relay2PID.SetMode(MANUAL);
    pidOutput1 = 0;
    pidOutput2 = 0;
    SLICE_DEBUG_PRINTLN(F("WATCHDOG TRIPPED: bus silent, relays off"));
}

void setupSlice()
{
    int rc;

    Serial.begin(115200);

    crumbs_arduino_init_peripheral(&ctx, I2C_ADR);
    crumbs_set_callbacks(&ctx, on_crumbs_message, nullptr, nullptr);

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

    rc = crumbs_register_handler(&ctx, BREAD_OP_SET_WATCHDOG, handler_set_watchdog, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register BREAD_OP_SET_WATCHDOG"));

    rc = crumbs_register_reply_handler(&ctx, 0x00, reply_version, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register version reply handler"));

    rc = crumbs_register_reply_handler(&ctx, RLHT_OP_GET_STATE, reply_get_state, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register RLHT_OP_GET_STATE reply handler"));

    rc = crumbs_register_reply_handler(&ctx, BREAD_OP_GET_CAPS, reply_get_caps, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register BREAD_OP_GET_CAPS reply handler"));

    rc = crumbs_register_reply_handler(&ctx, BREAD_OP_GET_WATCHDOG, reply_get_watchdog, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register BREAD_OP_GET_WATCHDOG reply handler"));

#ifdef RLHT_WATCHDOG_BOOT_MS
    // Integration opt-in: come up armed (e.g. e-stop wirings that power-cycle
    // the board). Default builds boot disarmed.
    wdTimeoutMs = (uint16_t)RLHT_WATCHDOG_BOOT_MS;
    wdLastRxMs = millis();
#endif

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
#if (RLHT_RELAY_COUNT >= 2)
    pinMode(RELAY2, OUTPUT);
#endif

    relaysLow();

    // Deterministic default mapping: Relay1->TC1, Relay2->TC2.
    // Controllers may override this later via RLHT_OP_SET_TC_SELECT.
    slice.relayHeater1.thermocoupleSelect = 1;
    slice.relayHeater2.thermocoupleSelect = 2;

    // Apply initial relay periods directly (no ISR active during setup).
    slice.relayHeater1.relayPeriod = clampRelayPeriod(slice.relayHeater1.relayPeriod);
    relay1PID.SetOutputLimits(0, slice.relayHeater1.relayPeriod);
    slice.relayHeater2.relayPeriod = clampRelayPeriod(slice.relayHeater2.relayPeriod);
    relay2PID.SetOutputLimits(0, slice.relayHeater2.relayPeriod);

    appliedMode = CLOSED_LOOP;
    apply_tunings_if_needed(slice.relayHeater1.Kp, slice.relayHeater1.Ki, slice.relayHeater1.Kd,
                            slice.relayHeater2.Kp, slice.relayHeater2.Ki, slice.relayHeater2.Kd);

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

        relaysLow();
        // As for a watchdog trip: a setpoint may arrive before the loop sees
        // this one at 0.
        relay1PID.SetMode(MANUAL);
        relay2PID.SetMode(MANUAL);
        pidOutput1 = 0;
        pidOutput2 = 0;

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

    // Snapshot ISR-written command fields atomically. Keep each masked
    // window short (<~10 us): a long cli window delays TWI ISR entry at the
    // SLA+R boundary and the resulting clock stretch is mishandled by Linux
    // SBC I2C masters (see feastorg/Slice_DCMT#3). Per-heater fields stay in
    // one window so a heater's tunings are never torn; a torn view across
    // windows self-corrects next iteration.
    noInterrupts();
    bool localEStop = slice.eStop || wdTripped;
    ControlMode localMode = slice.mode;
    double sp1 = slice.relayHeater1.setpointTemperature;
    double sp2 = slice.relayHeater2.setpointTemperature;
    double onTime1 = slice.relayHeater1.relayOnTime;
    double onTime2 = slice.relayHeater2.relayOnTime;
    interrupts();

    noInterrupts();
    double kp1 = slice.relayHeater1.Kp;
    double ki1 = slice.relayHeater1.Ki;
    double kd1 = slice.relayHeater1.Kd;
    uint8_t tc1 = slice.relayHeater1.thermocoupleSelect;
    interrupts();

    noInterrupts();
    double kp2 = slice.relayHeater2.Kp;
    double ki2 = slice.relayHeater2.Ki;
    double kd2 = slice.relayHeater2.Kd;
    uint8_t tc2 = slice.relayHeater2.thermocoupleSelect;
    interrupts();

    // The snapshot is read, never written back: the command handlers own
    // mode, setpoints, gains, thermocouple selects and the open-loop on-times,
    // and a write-back would overwrite a command that landed after the
    // snapshot (feastorg/Slice_RLHT#14).

    // A tripped command watchdog holds the same safe state as e-stop until
    // fresh traffic clears the trip (ISR side).
    if (localEStop)
    {
        relaysLow();
        noInterrupts();
        slice.relay1State = false;
        slice.relay2State = false;
        interrupts();
        return;
    }

    apply_mode_transition_if_needed(localMode);
    apply_tunings_if_needed(kp1, ki1, kd1, kp2, ki2, kd2);

    double drive1;
    double drive2;

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

        computeHeater(relay1PID, pidSetpoint1, pidInput1, pidOutput1, sp1, slice.relayHeater1.inputTemperature);

#if (RLHT_RELAY_COUNT >= 2)
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

        computeHeater(relay2PID, pidSetpoint2, pidInput2, pidOutput2, sp2, slice.relayHeater2.inputTemperature);
#endif

        drive1 = pidOutput1;
        drive2 = pidOutput2;

        // In closed loop the on-time is the PID's to publish. Skip it if a
        // SET_MODE open landed during this pass: the on-time is the open-duty
        // handler's from then on.
        noInterrupts();
        if (slice.mode == CLOSED_LOOP)
        {
            slice.relayHeater1.relayOnTime = pidOutput1;
#if (RLHT_RELAY_COUNT >= 2)
            slice.relayHeater2.relayOnTime = pidOutput2;
#endif
        }
        interrupts();
    }
    else if (localMode == OPEN_LOOP)
    {
        // In open loop, drive from the snapshot of the handler-written on-times.
        drive1 = onTime1;
        drive2 = onTime2;
    }
    else
    {
        relaysLow();
        noInterrupts();
        slice.relay1State = false;
        slice.relay2State = false;
        interrupts();
        SLICE_DEBUG_PRINTLN(F("ERROR INVALID MODE, ENTERED UNKNOWN STATE!"));
        return;
    }

    actuateRelay(RELAY1, timing.relay1Start, (unsigned long)slice.relayHeater1.relayPeriod, (unsigned long)drive1, slice.relay1State);
#if (RLHT_RELAY_COUNT >= 2)
    actuateRelay(RELAY2, timing.relay2Start, (unsigned long)slice.relayHeater2.relayPeriod, (unsigned long)drive2, slice.relay2State);
#else
    // Gen1 has no second heater: its snapshot fields go unused.
    (void)sp2;
    (void)tc2;
    (void)drive2;
#endif
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
