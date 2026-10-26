// Changing a heater's thermocouple select in closed loop doesn't kick the
// derivative (#54). With Kd alone and both sensors steady, the on-time is 0;
// a switch to a sensor 100 C colder must not read as a 100 C fall. Serial's
// status line shows the selected thermocouple's reading (#28).
#include "host.h"
#include "globals.h"
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

int main()
{
    setup();
    loop();

    host_tc1_celsius = 200.0f;
    host_tc2_celsius = 100.0f;
    const uint8_t pid[6] = {0, 0, 255, 0, 0, 255}; // Kd 25.5, u8 x10
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_PID, pid, 6);
    host_check("SET_PID is applied", host_read_ack().status == CRUMBS_ACK_APPLIED);
    const int16_t sp = 3000; // deci-degrees: 300.0 C
    const uint8_t sps[4] = {(uint8_t)(sp & 0xFF), (uint8_t)(sp >> 8), (uint8_t)(sp & 0xFF), (uint8_t)(sp >> 8)};
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_SETPOINTS, sps, 4);
    host_check("SET_SETPOINTS is applied", host_read_ack().status == CRUMBS_ACK_APPLIED);
    readings(5);
    host_check("steady sensors give no on-time", slice.relayHeater1.relayOnTime == 0);

    // Heater 1 moves from thermocouple 1 (200 C) to 2 (100 C).
    send_tc_select(2, 1);
    host_check("SET_TC_SELECT is applied", host_read_ack().status == CRUMBS_ACK_APPLIED);
    double peak = 0;
    for (int i = 0; i < 5; i++)
    {
        readings(1);
        if (slice.relayHeater1.relayOnTime > peak)
            peak = slice.relayHeater1.relayOnTime;
    }
    printf("     heater 1 on-time after the switch: up to %.1f ms\n", peak);
    host_check("a select change doesn't kick the derivative", peak == 0);

    // In open loop, after heater 1 moves back to thermocouple 1.
    const uint8_t open = RLHT_MODE_OPEN_LOOP;
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_MODE, &open, 1);
    (void)host_read_ack();
    send_tc_select(1, 2);
    (void)host_read_ack();
    readings(1);
    Serial.out.clear();
    printSliceState(Serial);
    host_check("the status line shows the selected reading in open loop",
               Serial.out.find("Relay1Input:200.00") != std::string::npos);

    return host_finish();
}
