#include <Arduino.h>
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
#if RLHT_HAS_STATUS_LED
#include <bsc_status_led.h>
#endif

static crumbs_context_t ctx;
#if RLHT_HAS_STATUS_LED
#if LED_PIN != 5
#error "bread-slice-core's status LED driver writes D5"
#endif
static bsc::StatusLed statusLed;
#endif

// Active LOW with the internal pull-up; release after 25 ms high.
bsc::EStop estopInput(ESTOP);
bsc::CommandWatchdog commandWatchdog;

RLHT_SLICE slice;
Timing timing = {0};
volatile uint8_t heaterFaults = 0;
volatile bool watchdogCommandsOnly = false;

void livenessStamp()
{
    if (!watchdogCommandsOnly)
        commandWatchdog.stamp(millis());
}

static_assert(RLHT_OVERTEMP_C > RLHT_SETPOINT_MAX_C,
              "RLHT_OVERTEMP_C must be above RLHT_SETPOINT_MAX_C, or a reachable setpoint faults");
static_assert(RLHT_NO_RISE_OUTPUT_PCT > 0 && RLHT_NO_RISE_OUTPUT_PCT <= 100,
              "RLHT_NO_RISE_OUTPUT_PCT must be 1 to 100");
static_assert(RLHT_OVERTEMP_READS >= 1 && RLHT_OVERTEMP_READS <= 255,
              "RLHT_OVERTEMP_READS must be 1 to 255");

static ControlMode appliedMode = CLOSED_LOOP;
static bool pidTuningsApplied = false;
static double lastKp1 = 0.0;
static double lastKi1 = 0.0;
static double lastKd1 = 0.0;
static double lastKp2 = 0.0;
static double lastKi2 = 0.0;
static double lastKd2 = 0.0;

// The contract's period range (rlht_ops.h). SET_PERIODS over I2C refuses a
// period outside it; the serial path and the boot defaults clamp to it.
static uint16_t clampRelayPeriod(uint16_t p)
{
    if (p < RLHT_PERIOD_MIN_MS)
        return RLHT_PERIOD_MIN_MS;
    if (p > RLHT_PERIOD_MAX_MS)
        return RLHT_PERIOD_MAX_MS;
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
// The thermocouple each PID last computed from. 0 until the first closed-loop
// pass, which then re-initialises the PIDs from their first input.
static uint8_t appliedTc1 = 0;
#if (RLHT_RELAY_COUNT >= 2)
static uint8_t appliedTc2 = 0;
#endif

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
    // e-stop: no external bias resistor on current boards, so use the internal
    // pull-up. Enable it first, so it has the rest of setup to charge the
    // line before the first sample below.
    estopInput.begin();
    setupSlice();
    setupRLHT();
    // I2C is live from setupSlice(), so sample the e-stop through the boot
    // second instead of sleeping: a line held at power-up, or pressed now,
    // is asserted before loop() runs. The status LED stays blue through it;
    // loop() shows the e-stop state from its first pass. The wait restamps
    // the command watchdog when it ends, so an RLHT_WATCHDOG_BOOT_MS under a
    // second does not trip on the first pass.
    bsc::bootWait(estopInput, 1000, processEStop, &commandWatchdog);
    wdt_enable(WDTO_1S);
}

void loop()
{
    wdt_reset();
    processEStop(estopInput.poll(millis()));
    updateStatusLed();
    watchdogLogic();
    if (measureThermocouples())
        checkHeaterFaults();
    relayControlLogic();
    serialCommands();
    printSerialOutput();
}

// The commands that keep a commands-only lease alive: the ones that start or
// change heating. SET_WATCHDOG, the lease's keepalive, refreshes it through
// arm() in its handler.
static bool is_actuating_op(uint8_t opcode)
{
    return opcode == RLHT_OP_SET_MODE || opcode == RLHT_OP_SET_SETPOINTS ||
           opcode == RLHT_OP_SET_OPEN_DUTY;
}

// Fires for every CRC-valid inbound command frame (SET_REPLY excluded by
// CRUMBS): any valid command proves a live master, so it stamps liveness.
// It does not clear a trip (feastorg/Slice_RLHT#9). The trip latches until
// an explicit clear: BREAD_OP_CLEAR_WATCHDOG_TRIP
// (handler_clear_watchdog_trip), the serial WDCLEAR command, or a reboot.
// SET_WATCHDOG re-arms without clearing (feastorg/Slice_DCMT#26), and
// RLHT_CAP_CLEAR_WATCHDOG_TRIP in GET_CAPS tells a controller so.
//
// Every valid frame stamps liveness, a refused one included: the watchdog
// watches for a live master, not for commands it accepts. on_message runs
// before the opcode's handler, so a frame whose opcode is not a command is
// refused here; CRUMBS cannot reject it itself, since on_message takes every
// frame.
//
// The commands are the opcodes setupSlice() registers a handler for, asked of
// the handler table itself: the contract's opcode tables also hold the GET
// opcodes, which are queries answered through SET_REPLY. A GET opcode sent as
// a command frame has no handler, so it is refused; it read APPLIED before
// (feastorg/Slice_RLHT#36). So is a command whose registration failed.
static void on_crumbs_message(crumbs_context_t *c, const crumbs_message_t *msg)
{
    if (!watchdogCommandsOnly || is_actuating_op(msg->opcode))
        commandWatchdog.stamp(millis());
    if (!crumbs_has_handler(c, msg->opcode))
        (void)crumbs_refuse(c, BREAD_REFUSE_UNKNOWN_OP);
}

void watchdogLogic()
{
    // True once, on the pass that trips: armed, not already tripped (the
    // relayControlLogic hold keeps the relays LOW while it is), and silent
    // for the timeout. commandWatchdog has latched the trip with interrupts
    // off before it returns, so the I2C handlers already refuse heating
    // commands.
    if (!commandWatchdog.check(millis()))
        return;

    relaysLow();

    // Same safe-state fields as processEStop.
    noInterrupts();
    slice.relayHeater1.setpointTemperature = 0;
    slice.relayHeater2.setpointTemperature = 0;
    slice.relayHeater1.relayOnTime = 0;
    slice.relayHeater2.relayOnTime = 0;
    slice.relay1State = false;
    slice.relay2State = false;
    interrupts();
    // relayControlLogic() skips computeHeater() while tripped, so it never
    // sees this setpoint at 0: drop the integral now. A setpoint sent after
    // the clear then starts the PID from an on-time of 0.
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
    // CRUMBS rejects a frame typed for another family before any handler
    // runs: the families' opcodes overlap, so one misaddressed would
    // otherwise be acted on as an RLHT command. Type 0x00 still matches.
    crumbs_set_type_id(&ctx, RLHT_TYPE_ID);
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

    rc = crumbs_register_handler(&ctx, BREAD_OP_CLEAR_WATCHDOG_TRIP, handler_clear_watchdog_trip, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register BREAD_OP_CLEAR_WATCHDOG_TRIP"));

    rc = crumbs_register_handler(&ctx, BREAD_OP_CLEAR_FAULTS, handler_clear_faults, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register BREAD_OP_CLEAR_FAULTS"));

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

    rc = crumbs_register_reply_handler(&ctx, BREAD_OP_GET_FAULTS, reply_get_faults, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register BREAD_OP_GET_FAULTS reply handler"));

#ifdef RLHT_WATCHDOG_BOOT_MS
    // Integration opt-in: come up armed (e.g. e-stop wirings that power-cycle
    // the board). Default builds boot disarmed.
    commandWatchdog.arm((uint16_t)RLHT_WATCHDOG_BOOT_MS, millis());
#endif

#if RLHT_HAS_STATUS_LED
    statusLed.begin();
    statusLed.set(bsc::StatusLed::Boot);
#endif

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

// Red while the e-stop is held, green otherwise; StatusLed writes only on a
// change. Called from loop() only. The boot wait in setup() polls the e-stop
// too, and when that wrote the LED, its first pass replaced the boot blue
// within microseconds.
void updateStatusLed()
{
#if RLHT_HAS_STATUS_LED
    statusLed.set(estopInput.asserted() ? bsc::StatusLed::EStop : bsc::StatusLed::Running);
#endif
}

// The line is sampled every pass by estopInput.poll(), not edge-triggered.
// The first LOW sample asserts the e-stop, so bounce or an edge train cannot
// postpone a stop (#29); release needs the line HIGH for 25 ms without a
// single LOW sample. By the time Pressed arrives here, estopInput has set
// asserted() and counted the press with interrupts off, so the I2C handlers
// already refuse heating commands.
void processEStop(bsc::EStop::Event e)
{
    if (e == bsc::EStop::Pressed)
    {
        noInterrupts();
        slice.relayHeater1.setpointTemperature = 0;
        slice.relayHeater2.setpointTemperature = 0;
        slice.relayHeater1.relayOnTime = 0;
        slice.relayHeater2.relayOnTime = 0;
        slice.relay1State = false;
        slice.relay2State = false;
        interrupts();

        relaysLow();
        // The loop skips computeHeater() while the e-stop is held, so it may
        // never see these setpoints at 0: drop the integral now, so heating
        // restarts from 0 when a setpoint returns after the release.
        relay1PID.SetMode(MANUAL);
        relay2PID.SetMode(MANUAL);
        pidOutput1 = 0;
        pidOutput2 = 0;

        SLICE_DEBUG_PRINTLN(F("ESTOP PRESSED!"));
    }
    else if (e == bsc::EStop::Released)
    {
        // Nothing heats: setpoints and on-times stay 0 until new ones arrive.
        SLICE_DEBUG_PRINTLN(F("ESTOP RELEASED!"));
    }
}

// True on a pass that took a new reading.
bool measureThermocouples()
{
    if (millis() - timing.lastThermoRead >= THERMO_UPDATE_TIME_MS)
    {
        slice.temperature1 = thermocouple1.readCelsius();
        slice.temperature2 = thermocouple2.readCelsius();
        timing.lastThermoRead += THERMO_UPDATE_TIME_MS;
        return true;
    }
    return false;
}

double selectedTemperature(uint8_t thermocoupleSelect)
{
    return (thermocoupleSelect == 2) ? slice.temperature2 : slice.temperature1;
}

// Latch fault bits and hold the safe state the e-stop and the watchdog trip
// hold: relays off, setpoints and on-times 0, PIDs in manual. The flag is set
// in the same masked window, so the I2C handlers refuse from then on. Clearing
// resumes nothing: the setpoints are 0 until new ones arrive, and a PID put
// back in automatic starts from 0.
static void latchFaults(uint8_t bits)
{
    noInterrupts();
    heaterFaults |= bits;
    slice.relayHeater1.setpointTemperature = 0;
    slice.relayHeater2.setpointTemperature = 0;
    slice.relayHeater1.relayOnTime = 0;
    slice.relayHeater2.relayOnTime = 0;
    slice.relay1State = false;
    slice.relay2State = false;
    interrupts();

    relaysLow();
    relay1PID.SetMode(MANUAL);
    relay2PID.SetMode(MANUAL);
    pidOutput1 = 0;
    pidOutput2 = 0;
    SLICE_DEBUG_PRINTLN(F("HEATER FAULT: relays off until FAULTCLEAR"));
}

// Per-heater state for the no-rise and sensor checks, main loop only.
struct FaultWatch
{
    bool rising = false; // a no-rise window is open
    unsigned long windowStart = 0;
    double windowStartTemp = 0.0;
    uint8_t nanReads = 0;
    uint8_t hotReads = 0;
};
static FaultWatch watch1;
static FaultWatch watch2;

// The fault bits one heater sets this reading. `temp` is its thermocouple's
// reading; `active` is true in closed loop with a setpoint and no hold, the
// only state in which it depends on that reading.
static uint8_t heaterFaultBits(FaultWatch &w, double temp, bool active, double output, uint16_t period,
                               uint8_t overtempBit, uint8_t noRiseBit, uint8_t sensorBit)
{
    uint8_t bits = 0;

    if (!isnan(temp) && temp > RLHT_OVERTEMP_C)
    {
        if (w.hotReads < RLHT_OVERTEMP_READS)
            w.hotReads++;
        if (w.hotReads >= RLHT_OVERTEMP_READS)
            bits |= overtempBit;
    }
    else
    {
        w.hotReads = 0;
    }

    if (active && isnan(temp))
    {
        if (w.nanReads < RLHT_SENSOR_FAULT_READS)
            w.nanReads++;
        if (w.nanReads >= RLHT_SENSOR_FAULT_READS)
            bits |= sensorBit;
    }
    else
    {
        w.nanReads = 0;
    }

    // The window opens when the output reaches the threshold and restarts
    // whenever the temperature has risen by the delta, so it faults only
    // after RLHT_NO_RISE_MS at near-full output without that rise.
    const double nearFull = (double)period * RLHT_NO_RISE_OUTPUT_PCT / 100.0;
    if (active && !isnan(temp) && period > 0 && output >= nearFull)
    {
        const unsigned long now = millis();
        if (!w.rising || temp - w.windowStartTemp >= RLHT_NO_RISE_DELTA_C)
        {
            w.rising = true;
            w.windowStart = now;
            w.windowStartTemp = temp;
        }
        else if (now - w.windowStart >= RLHT_NO_RISE_MS)
        {
            bits |= noRiseBit;
        }
    }
    else
    {
        w.rising = false;
    }

    return bits;
}

// Runs on each new thermocouple reading (#48, RLHT_CAP_FAULTS).
void checkHeaterFaults()
{
    noInterrupts();
    const bool hold = estopInput.asserted() || commandWatchdog.tripped() || heaterFaults != 0;
    const bool closed = (slice.mode == CLOSED_LOOP);
    const double sp1 = slice.relayHeater1.setpointTemperature;
    const double sp2 = slice.relayHeater2.setpointTemperature;
    const uint8_t tc1 = slice.relayHeater1.thermocoupleSelect;
    const uint8_t tc2 = slice.relayHeater2.thermocoupleSelect;
    interrupts();

    const double t1 = selectedTemperature(tc1);
    uint8_t bits = heaterFaultBits(watch1, t1, closed && sp1 > 0 && !hold, pidOutput1,
                                   slice.relayHeater1.relayPeriod,
                                   RLHT_FAULT_H1_OVERTEMP, RLHT_FAULT_H1_NO_RISE, RLHT_FAULT_H1_SENSOR);
#if (RLHT_RELAY_COUNT >= 2)
    const double t2 = selectedTemperature(tc2);
    bits |= heaterFaultBits(watch2, t2, closed && sp2 > 0 && !hold, pidOutput2,
                            slice.relayHeater2.relayPeriod,
                            RLHT_FAULT_H2_OVERTEMP, RLHT_FAULT_H2_NO_RISE, RLHT_FAULT_H2_SENSOR);
#else
    // Gen1 has no second heater to fault.
    (void)sp2;
    (void)tc2;
    (void)watch2;
#endif

    if (bits != 0 && (bits & ~heaterFaults) != 0)
        latchFaults(bits);
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
    bool localEStop = estopInput.asserted() || commandWatchdog.tripped() || heaterFaults != 0;
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

    // A tripped command watchdog or a latched fault holds the same safe state
    // as e-stop until an explicit clear (CLEAR_WATCHDOG_TRIP or serial
    // WDCLEAR; CLEAR_FAULTS or serial FAULTCLEAR).
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
        // A thermocouple select change swaps a PID's input to another sensor.
        // Putting the PID in manual makes computeHeater()'s switch back to
        // automatic re-initialise it from the new input and the current
        // output, so the difference between the sensors isn't a step in the
        // measurement for the derivative to kick on (feastorg/Slice_RLHT#54).
        if (tc1 != appliedTc1)
        {
            relay1PID.SetMode(MANUAL);
            appliedTc1 = tc1;
        }
        computeHeater(relay1PID, pidSetpoint1, pidInput1, pidOutput1, sp1, selectedTemperature(tc1));

#if (RLHT_RELAY_COUNT >= 2)
        if (tc2 != appliedTc2)
        {
            relay2PID.SetMode(MANUAL);
            appliedTc2 = tc2;
        }
        computeHeater(relay2PID, pidSetpoint2, pidInput2, pidOutput2, sp2, selectedTemperature(tc2));
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
