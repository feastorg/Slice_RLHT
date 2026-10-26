// A heater's thermocouple select can't change while it runs closed loop with
// a setpoint (#54): the swap would read as a step in the measurement. It
// can with the setpoint at 0, and the heater then starts from the new
// sensor with no kick. Serial's status line shows the selected
// thermocouple's reading (#28).
#include "host.h"
#include "globals.h"
#include <bread/bread_refuse.h>
#include <bread/rlht_ops.h>
#include <cstdio>
#include <string>

void setup();
void loop();

static void readings(int n)
{
    for (int i = 0; i < n; i++)
    {
        host_now += THERMO_UPDATE_TIME_MS;
        loop();
    }
}

static void send_tc_select(uint8_t tc1, uint8_t tc2)
{
    const uint8_t p[2] = {tc1, tc2};
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_TC_SELECT, p, 2);
}

static void send_setpoints(int16_t sp1_deci_c, int16_t sp2_deci_c)
{
    const uint8_t p[4] = {(uint8_t)(sp1_deci_c & 0xFF), (uint8_t)((uint16_t)sp1_deci_c >> 8),
                          (uint8_t)(sp2_deci_c & 0xFF), (uint8_t)((uint16_t)sp2_deci_c >> 8)};
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_SETPOINTS, p, 4);
}

static bool refused(uint8_t reason)
{
    const crumbs_ack_t a = host_read_ack();
    return a.status == CRUMBS_ACK_REFUSED && a.reason == reason;
}

static bool applied() { return host_read_ack().status == CRUMBS_ACK_APPLIED; }

// The largest on-time over n readings.
static double peak_on1(int n)
{
    double peak = 0;
    for (int i = 0; i < n; i++)
    {
        readings(1);
        if (slice.relayHeater1.relayOnTime > peak)
            peak = slice.relayHeater1.relayOnTime;
    }
    return peak;
}

int main()
{
    setup();
    loop();

    host_tc1_celsius = 200.0f;
    host_tc2_celsius = 100.0f;

    // ---- Open loop: selects change freely, and the status line shows the
    // selected reading.
    const uint8_t open = RLHT_MODE_OPEN_LOOP;
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_MODE, &open, 1);
    (void)host_read_ack();
    send_tc_select(2, 1);
    host_check("SET_TC_SELECT in open loop is applied", applied());
    readings(1);
    Serial.out.clear();
    printSliceState(Serial);
    host_check("the status line shows the selected reading in open loop",
               Serial.out.find("Relay1Input:100.00") != std::string::npos);
    send_tc_select(1, 2);
    (void)host_read_ack();
    const uint8_t closed = RLHT_MODE_CLOSED_LOOP;
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_MODE, &closed, 1);
    (void)host_read_ack();

    const uint8_t pid[6] = {0, 0, 255, 0, 0, 255}; // Kd 25.5, u8 x10
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_PID, pid, 6);
    host_check("SET_PID is applied", applied());
    send_setpoints(3000, 3000); // deci-degrees: 300.0 C
    host_check("SET_SETPOINTS is applied", applied());
    readings(5);
    host_check("steady sensors give no on-time", slice.relayHeater1.relayOnTime == 0);

    // ---- Both heaters heating: a change is refused, the select in force is
    // applied.
    send_tc_select(2, 1);
    host_check("SET_TC_SELECT while both heat is refused WRONG_MODE", refused(BREAD_REFUSE_WRONG_MODE));
    host_check("neither select changes",
               slice.relayHeater1.thermocoupleSelect == 1 && slice.relayHeater2.thermocoupleSelect == 2);
    send_tc_select(1, 2);
    host_check("resending the selects in force is applied", applied());
    printf("     heater 1 on-time after the refused change: up to %.1f ms\n", peak_on1(5));

    // ---- Heater 1 off: its select changes, heater 2's is still refused.
    send_setpoints(0, 3000);
    (void)host_read_ack();
    send_tc_select(2, 1);
    host_check("with heater 1 off, the frame is refused for heater 2", refused(BREAD_REFUSE_WRONG_MODE));
    host_check("heater 1 moves to thermocouple 2, heater 2 stays on 2",
               slice.relayHeater1.thermocoupleSelect == 2 && slice.relayHeater2.thermocoupleSelect == 2);

    // Heater 1 back on, from thermocouple 2 at 100 C: no kick from the 100 C
    // between the sensors.
    send_setpoints(3000, 3000);
    (void)host_read_ack();
    const double peak = peak_on1(5);
    printf("     heater 1 on-time after restarting on the new sensor: up to %.1f ms\n", peak);
    host_check("restarting on the new sensor doesn't kick the derivative", peak == 0);

    // ---- With Kp 1.0 and both sensors at 200 C against 300 C, the on-time
    // is 100 ms from either sensor, before a change and after it.
    const uint8_t kp[6] = {10, 0, 0, 10, 0, 0}; // Kp 1.0, u8 x10
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_PID, kp, 6);
    (void)host_read_ack();
    host_tc2_celsius = 200.0f;
    readings(2);
    const double before = slice.relayHeater1.relayOnTime;
    send_setpoints(0, 3000);
    (void)host_read_ack();
    send_tc_select(1, 2);
    (void)host_read_ack();
    send_setpoints(3000, 3000);
    (void)host_read_ack();
    readings(3);
    printf("     Kp 1.0: on-time %.1f ms before the change, %.1f ms after\n", before, slice.relayHeater1.relayOnTime);
    host_check("a change with the heater off carries no P term over",
               before == 100.0 && slice.relayHeater1.relayOnTime == 100.0);
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_PID, pid, 6);
    (void)host_read_ack();
    host_tc2_celsius = 100.0f;
    send_setpoints(0, 3000);
    (void)host_read_ack();
    send_tc_select(2, 2);
    (void)host_read_ack();
    send_setpoints(3000, 3000);
    (void)host_read_ack();
    readings(2);

    // ---- Serial follows the same rule.
    Serial.out.clear();
    Serial.in += "R1TC=1\n";
    loop();
    host_check("serial R1TC= is ignored while heater 1 heats",
               Serial.out.find("Ignored: R1 heating in closed loop") != std::string::npos &&
                   slice.relayHeater1.thermocoupleSelect == 2);
    Serial.out.clear();
    Serial.in += "R1TC=2\n";
    loop();
    host_check("serial R1TC= with the select in force is accepted",
               Serial.out.find("R1 TC: 2") != std::string::npos);

    return host_finish();
}
