// The loop opens a masked window only on a new thermocouple reading or once
// RLHT_SNAPSHOT_INTERVAL_MS has passed, not on every pass. A window on every
// pass delayed the TWI interrupt often enough to cut GET_STATE replies short
// (feastorg/Slice_RLHT#81). A command still reaches the relays within the
// interval, and the e-stop still drops them on the next pass.
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

    // Closed loop at 300 C against 25 C: both heaters at full on-time.
    const uint8_t closed = RLHT_MODE_CLOSED_LOOP;
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_MODE, &closed, 1);
    (void)host_read_ack();
    const int16_t sp = 3000; // deci-degrees
    const uint8_t sps[4] = {(uint8_t)(sp & 0xFF), (uint8_t)(sp >> 8), (uint8_t)(sp & 0xFF), (uint8_t)(sp >> 8)};
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_SETPOINTS, sps, 4);
    host_check("SET_SETPOINTS is applied", host_read_ack().status == CRUMBS_ACK_APPLIED);

    // 3 s of 1 ms passes (each loop() also reads the e-stop, +1 ms).
    int passes = 0, masked = 0, high = 0;
    const unsigned long start = host_now;
    while (host_now - start < 3000)
    {
        const unsigned long before = host_masked_windows;
        loop();
        host_now++;
        passes++;
        if (host_masked_windows != before)
            masked++;
        if (host_pin_level[RELAY1] == HIGH)
            high++;
    }
    const unsigned long ms = host_now - start;
    const int allowed = (int)(ms / RLHT_SNAPSHOT_INTERVAL_MS) + (int)(ms / THERMO_UPDATE_TIME_MS) + 2;
    printf("     %d of %d passes opened a masked window over %lu ms (allowed %d); relay 1 high on %d\n",
           masked, passes, ms, allowed, high);
    host_check("a masked window opens only on a snapshot or a reading, not every pass", masked <= allowed);
    // Kp 1 against a 275 C error: 275 ms on in each 1000 ms period.
    host_check("closed loop still switches relay 1", high > 0 && high < passes);

    // Open loop at full duty, until relay 1 is on.
    const uint8_t open = RLHT_MODE_OPEN_LOOP;
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_MODE, &open, 1);
    (void)host_read_ack();
    const uint8_t duty100[2] = {100, 100};
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_OPEN_DUTY, duty100, 2);
    host_check("SET_OPEN_DUTY 100 is applied", host_read_ack().status == CRUMBS_ACK_APPLIED);
    for (int i = 0; i < 2 * RLHT_SNAPSHOT_INTERVAL_MS; i++)
    {
        loop();
        host_now++;
    }
    host_check("full duty drives relay 1", host_pin_level[RELAY1] == HIGH);

    // A command reaches the relays within the interval: duty 0 drops relay 1
    // within RLHT_SNAPSHOT_INTERVAL_MS plus a pass.
    const uint8_t duty0[2] = {0, 0};
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_OPEN_DUTY, duty0, 2);
    host_check("SET_OPEN_DUTY 0 is applied", host_read_ack().status == CRUMBS_ACK_APPLIED);
    const unsigned long sent = host_now;
    int waited = 0;
    while (host_pin_level[RELAY1] == HIGH && waited < 1000)
    {
        loop();
        host_now++;
        waited++;
    }
    printf("     relay 1 low %lu ms after SET_OPEN_DUTY 0\n", host_now - sent);
    host_check("a command drives the relay within the snapshot interval",
               host_pin_level[RELAY1] == LOW && host_now - sent <= RLHT_SNAPSHOT_INTERVAL_MS + 4);

    // Full duty again, then the e-stop: relays low on the next pass.
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_OPEN_DUTY, duty100, 2);
    (void)host_read_ack();
    for (int i = 0; i < 2 * RLHT_SNAPSHOT_INTERVAL_MS; i++)
    {
        loop();
        host_now++;
    }
    host_check("full duty drives relay 1 again", host_pin_level[RELAY1] == HIGH);
    host_estop_level = LOW;
    loop();
    host_check("the e-stop drops relay 1 on the next pass", host_pin_level[RELAY1] == LOW);
    host_estop_level = HIGH;

    return host_finish();
}
