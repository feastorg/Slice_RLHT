// The heater PIDs compute once per thermocouple reading (#53). With Kd
// alone and a temperature that falls by the same step every reading, each
// compute's output is that step's derivative, so the on-time holds one value
// from reading to reading. A PID computing faster than the readings shows
// the derivative as an impulse, then zero until the next reading. The
// integral, with the gains scaled to the reading interval, adds what Ki
// sets per second.
#include "host.h"
#include "globals.h"
#include <bread/rlht_ops.h>
#include <cstdio>

void setup();
void loop();

int main()
{
    setup();
    loop();

    const uint8_t pid[6] = {0, 0, 255, 0, 0, 255}; // Kd 25.5, u8 x10
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_PID, pid, 6);
    host_check("SET_PID is applied", host_read_ack().status == CRUMBS_ACK_APPLIED);
    const uint8_t mode = RLHT_MODE_CLOSED_LOOP;
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_MODE, &mode, 1);
    (void)host_read_ack();
    const int16_t sp = 3000; // deci-degrees: 300.0 C
    const uint8_t sps[4] = {(uint8_t)(sp & 0xFF), (uint8_t)(sp >> 8), 0, 0};
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_SETPOINTS, sps, 4);
    host_check("SET_SETPOINTS is applied", host_read_ack().status == CRUMBS_ACK_APPLIED);

    // 1 C per reading: Kd 25.5 x 1 C / 0.3 s is 85 ms of on-time.
    const double expected = 25.5 * 1.0 / (THERMO_UPDATE_TIME_MS / 1000.0);
    // The next step is set once a reading has taken the last one, so every
    // reading sees one step. 7 ms passes don't divide the reading interval.
    float t = 290.0f;
    host_tc1_celsius = host_tc2_celsius = t;
    int passes = 0, off = 0;
    double lo = 1e9, hi = -1e9;
    while (passes < 6000)
    {
        host_now += 7;
        loop();
        if (slice.temperature1 == t)
        {
            t -= 1.0f;
            host_tc1_celsius = host_tc2_celsius = t;
        }
        // Skip the first readings, while the derivative has no previous one.
        if (passes++ < 200)
            continue;
        const double on = slice.relayHeater1.relayOnTime;
        if (on < expected - 1)
            off++;
        if (on < lo)
            lo = on;
        if (on > hi)
            hi = on;
    }
    printf("     on-time %.1f to %.1f ms over %d passes, %d below %.1f ms\n", lo, hi, passes - 200, off, expected);
    host_check("the on-time holds the derivative of one reading between readings",
               off == 0 && hi - lo < 1.0 && hi > expected - 1 && hi < expected + 1);

    // The integral per second is the one Ki sets: Ki 1.0 against a steady
    // 10 C error adds 10 ms of on-time a second.
    const uint8_t zero[4] = {0, 0, 0, 0};
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_SETPOINTS, zero, 4); // heater off: output 0
    (void)host_read_ack();
    const uint8_t ki[6] = {0, 10, 0, 0, 10, 0}; // Ki 1.0, u8 x10
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_PID, ki, 6);
    host_check("SET_PID is applied", host_read_ack().status == CRUMBS_ACK_APPLIED);
    host_tc1_celsius = host_tc2_celsius = 290.0f;
    while (slice.temperature1 != 290.0f) // a reading at 290 C before heating
    {
        host_now += 7;
        loop();
    }
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_SETPOINTS, sps, 4);
    (void)host_read_ack();
    const unsigned long start = host_now;
    while (host_now - start < 30000)
    {
        host_now += 7;
        loop();
    }
    const double rate = slice.relayHeater1.relayOnTime / ((host_now - start) / 1000.0);
    printf("     integral: %.2f ms of on-time per second, expected 10.00\n", rate);
    host_check("Ki 1.0 at a 10 C error adds 10 ms of on-time a second", rate > 9.5 && rate < 10.5);

    return host_finish();
}
