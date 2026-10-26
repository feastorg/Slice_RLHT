// Heater faults latch and hold both heaters off until a clear (#48,
// RLHT_CAP_FAULTS): an open thermocouple in closed loop, an over-temperature
// in either mode, and no rise at near-full output. GET_FAULTS and GET_STATE
// report them, heat-starting commands are refused FAULT, CLEAR_FAULTS and
// serial FAULTCLEAR clear them and resume nothing.
#include "host.h"
#include "globals.h"
#include <bread/bread_caps.h>
#include <bread/rlht_ops.h>
#include <cmath>

void setup();
void loop();

static void send_setpoints(int16_t sp1_deci_c, int16_t sp2_deci_c)
{
    const uint8_t p[4] = {(uint8_t)(sp1_deci_c & 0xFF), (uint8_t)((uint16_t)sp1_deci_c >> 8),
                          (uint8_t)(sp2_deci_c & 0xFF), (uint8_t)((uint16_t)sp2_deci_c >> 8)};
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_SETPOINTS, p, 4);
}

static void send_mode(uint8_t mode) { host_send(RLHT_TYPE_ID, RLHT_OP_SET_MODE, &mode, 1); }

static void send_open_duty(uint8_t d1, uint8_t d2)
{
    const uint8_t p[2] = {d1, d2};
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_OPEN_DUTY, p, 2);
}

static void send_clear() { host_send(RLHT_TYPE_ID, BREAD_OP_CLEAR_FAULTS, nullptr, 0); }

static bool applied() { return host_read_ack().status == CRUMBS_ACK_APPLIED; }

static bool refused(uint8_t reason)
{
    const crumbs_ack_t a = host_read_ack();
    return a.status == CRUMBS_ACK_REFUSED && a.reason == reason;
}

static uint8_t faults()
{
    crumbs_message_t reply;
    bread_faults_result_t f = {0xFF};
    if (!host_query(BREAD_OP_GET_FAULTS, &reply) || bread_faults_parse_payload(reply.data, reply.data_len, &f) != 0)
        host_check("GET_FAULTS answers", false);
    return f.faults;
}

static uint8_t state_flags()
{
    crumbs_message_t reply;
    rlht_state_result_t s = {};
    if (!host_query(RLHT_OP_GET_STATE, &reply) || rlht_parse_state_payload(reply.data, reply.data_len, &s) != 0)
        host_check("GET_STATE answers", false);
    return s.flags;
}

static uint16_t on1_ms()
{
    crumbs_message_t reply;
    rlht_state_result_t s = {};
    if (!host_query(RLHT_OP_GET_STATE, &reply) || rlht_parse_state_payload(reply.data, reply.data_len, &s) != 0)
        host_check("GET_STATE answers", false);
    return s.on1_ms;
}

static bool relays_off()
{
#if (RLHT_RELAY_COUNT >= 2)
    return host_pin_level[RELAY1] == LOW && host_pin_level[RELAY2] == LOW;
#else
    return host_pin_level[RELAY1] == LOW;
#endif
}

// One thermocouple reading per call: THERMO_UPDATE_TIME_MS, then a pass.
static void readings(int n)
{
    for (int i = 0; i < n; i++)
    {
        host_now += THERMO_UPDATE_TIME_MS;
        loop();
    }
}

// Every query stamps the watchdog, which is disarmed here, so passes can run
// unattended.
static void run_seconds(int seconds)
{
    for (int i = 0; i < seconds; i++)
    {
        host_now += 1000;
        loop();
    }
}

static void clear_and_reset_inputs()
{
    send_clear();
    (void)host_read_ack();
    host_tc1_celsius = 25.0f;
    host_tc2_celsius = 25.0f;
    readings(2);
}

int main()
{
    setup();
    loop();

    crumbs_message_t reply;
    bread_caps_result_t caps;
    host_check("GET_CAPS advertises RLHT_CAP_FAULTS",
               host_query(BREAD_OP_GET_CAPS, &reply) && bread_caps_parse_payload(reply.data, reply.data_len, &caps) == 0 &&
                   (caps.flags & RLHT_CAP_FAULTS) != 0);
    host_check("no fault at boot", faults() == 0 && !(state_flags() & RLHT_FLAG_FAULT));

    // ---- An open thermocouple on a heater with no setpoint is not a fault.
    host_tc1_celsius = NAN;
    host_tc2_celsius = NAN;
    readings(5);
    host_check("an open thermocouple on an idle heater latches nothing", faults() == 0);

    // ---- Sensor: closed loop with a setpoint, RLHT_SENSOR_FAULT_READS NaN
    // readings in a row.
    host_tc2_celsius = 25.0f;
    send_setpoints(400, 0); // deci-degrees: 40.0 C
    host_check("a setpoint is applied", applied());
    readings(RLHT_SENSOR_FAULT_READS - 1);
    host_check("fewer NaN readings than the limit latch nothing", faults() == 0);
    readings(1);
    host_check("RLHT_SENSOR_FAULT_READS NaN readings latch H1_SENSOR", faults() == RLHT_FAULT_H1_SENSOR);
    host_check("GET_STATE sets RLHT_FLAG_FAULT", (state_flags() & RLHT_FLAG_FAULT) != 0);
    host_check("the fault zeroes the setpoints",
               slice.relayHeater1.setpointTemperature == 0 && slice.relayHeater2.setpointTemperature == 0);
    loop();
    host_check("the relays are off", relays_off());

    send_setpoints(400, 0);
    host_check("SET_SETPOINTS is refused FAULT", refused(BREAD_REFUSE_FAULT));
    send_mode(RLHT_MODE_OPEN_LOOP);
    host_check("SET_MODE is refused FAULT", refused(BREAD_REFUSE_FAULT));
    send_open_duty(100, 100);
    host_check("SET_OPEN_DUTY is refused FAULT", refused(BREAD_REFUSE_FAULT));

    const uint8_t one = 0;
    host_send(RLHT_TYPE_ID, BREAD_OP_CLEAR_FAULTS, &one, 1);
    host_check("CLEAR_FAULTS with a payload is refused MALFORMED", refused(BREAD_REFUSE_MALFORMED));
    host_check("and the fault stays latched", faults() == RLHT_FAULT_H1_SENSOR);

    send_clear();
    host_check("CLEAR_FAULTS is applied", applied());
    host_check("and clears the fault", faults() == 0 && !(state_flags() & RLHT_FLAG_FAULT));
    readings(5);
    host_check("the clear heats nothing: the setpoint is still 0",
               faults() == 0 && slice.relayHeater1.setpointTemperature == 0 && relays_off());

    // ---- Over-temperature, in open loop, which never reads the temperature
    // for control.
    host_tc1_celsius = 25.0f;
    readings(1);
    send_mode(RLHT_MODE_OPEN_LOOP);
    (void)host_read_ack();
    send_open_duty(100, 100);
    host_check("open loop at 100 % is applied", applied());
    loop();
    host_check("open loop heats", host_pin_level[RELAY1] == HIGH);
    host_tc1_celsius = RLHT_OVERTEMP_C + 5.0f;
    readings(1);
    host_check("above RLHT_OVERTEMP_C in open loop latches H1_OVERTEMP", faults() & RLHT_FAULT_H1_OVERTEMP);
    loop();
    host_check("the relays go off", relays_off() && slice.relayHeater1.relayOnTime == 0);
    // The loop's hold is the backstop for a path that misses the refusal: an
    // on-time set behind it still drives nothing while the fault is latched.
    slice.relayHeater1.relayOnTime = slice.relayHeater1.relayPeriod;
    loop();
    host_check("the hold keeps the relays off even with an on-time set", relays_off());
    slice.relayHeater1.relayOnTime = 0;
    send_clear();
    (void)host_read_ack();
    readings(1);
    host_check("a clear while still over temperature latches again", faults() & RLHT_FAULT_H1_OVERTEMP);
    host_tc1_celsius = RLHT_OVERTEMP_C;
    send_clear();
    (void)host_read_ack();
    readings(2);
    host_check("at RLHT_OVERTEMP_C itself nothing latches", faults() == 0);
    clear_and_reset_inputs();

    // ---- Precedence: an e-stop held over a latched fault refuses ESTOP.
    host_tc1_celsius = RLHT_OVERTEMP_C + 5.0f;
    readings(1);
    host_estop_level = LOW;
    loop();
    send_open_duty(10, 10);
    host_check("with an e-stop and a fault, a SET is refused ESTOP", refused(BREAD_REFUSE_ESTOP));
    host_estop_level = HIGH;
    host_now += 30;
    loop();
    loop();
    clear_and_reset_inputs();
    host_check("after the release and a clear, no fault", faults() == 0);

    // ---- Serial: commands are ignored while latched, FAULTCLEAR clears.
    send_mode(RLHT_MODE_CLOSED_LOOP);
    (void)host_read_ack();
    host_tc1_celsius = RLHT_OVERTEMP_C + 5.0f;
    readings(1);
    Serial.out.clear();
    Serial.in += "R1TEMP=40\n";
    loop();
    host_check("serial R1TEMP= is ignored while a fault is latched",
               Serial.out.find("heater fault latched") != std::string::npos && slice.relayHeater1.setpointTemperature == 0);
    host_tc1_celsius = 25.0f;
    Serial.out.clear();
    Serial.in += "FAULTCLEAR\n";
    loop();
    host_check("serial FAULTCLEAR clears", Serial.out.find("Faults cleared") != std::string::npos && faults() == 0);
    readings(2);

    // ---- No rise: closed loop at full output with the temperature flat.
    // Kp 10 against a 380 C error holds the output at the 1000 ms period.
    const uint8_t pid[6] = {100, 0, 0, 100, 0, 0};
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_PID, pid, 6);
    (void)host_read_ack();
    host_tc1_celsius = 20.0f;
    send_setpoints(4000, 0); // deci-degrees: 400.0 C
    (void)host_read_ack();
    run_seconds((int)(RLHT_NO_RISE_MS / 1000) - 10);
    host_check("at full output, short of RLHT_NO_RISE_MS, nothing latches", faults() == 0 && on1_ms() >= 900);
    run_seconds(15);
    host_check("past RLHT_NO_RISE_MS with no rise, H1_NO_RISE latches", faults() == RLHT_FAULT_H1_NO_RISE);
    clear_and_reset_inputs();

    // A heater that warms by more than the delta per window never faults.
    host_tc1_celsius = 20.0f;
    send_setpoints(4000, 0);
    (void)host_read_ack();
    for (int window = 0; window < 4; window++)
    {
        run_seconds((int)(RLHT_NO_RISE_MS / 1000) / 2);
        host_tc1_celsius += (float)RLHT_NO_RISE_DELTA_C + 1.0f;
    }
    host_check("a heater rising by more than RLHT_NO_RISE_DELTA_C per half window never faults", faults() == 0);
    send_setpoints(0, 0);
    (void)host_read_ack();
    readings(2);

    // ---- Heater 2: gen2 checks it, gen1 has no second heater.
    host_tc2_celsius = NAN;
    send_setpoints(0, 400);
    (void)host_read_ack();
    readings(RLHT_SENSOR_FAULT_READS + 1);
#if (RLHT_RELAY_COUNT >= 2)
    host_check("heater 2 latches H2_SENSOR", faults() == RLHT_FAULT_H2_SENSOR);
#else
    host_check("gen1 checks no heater 2", faults() == 0);
#endif

    return host_finish();
}
