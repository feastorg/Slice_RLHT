// A command-watchdog trip turns the relays off, latches through a re-arm and
// refuses heating commands until CLEAR_WATCHDOG_TRIP, and the clear heats
// nothing. The I2C half of a bench watchdog step.
#include "host.h"
#include "globals.h"
#include <bread/rlht_ops.h>
#include <crumbs_message_helpers.h>

void setup();
void loop();

static void open_duty(uint8_t d1, uint8_t d2)
{
    crumbs_message_t m;
    crumbs_msg_init(&m, RLHT_TYPE_ID, RLHT_OP_SET_OPEN_DUTY);
    rlht_set_open_duty_t v = {d1, d2};
    rlht_set_open_duty_pack(&m, &v);
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_OPEN_DUTY, m.data, m.data_len);
}

static void mode(uint8_t md)
{
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_MODE, &md, 1);
}

static void arm(uint16_t ms)
{
    const uint8_t d[2] = {(uint8_t)(ms & 0xFF), (uint8_t)(ms >> 8)};
    host_send(RLHT_TYPE_ID, BREAD_OP_SET_WATCHDOG, d, 2);
}

// Both queries stamp the watchdog, as any reply build does.
static rlht_state_result_t state()
{
    crumbs_message_t reply;
    rlht_state_result_t s = {};
    if (!host_query(RLHT_OP_GET_STATE, &reply) || rlht_parse_state_payload(reply.data, reply.data_len, &s) != 0)
        host_check("GET_STATE answers", false);
    return s;
}

static bread_watchdog_result_t wdog()
{
    crumbs_message_t reply;
    bread_watchdog_result_t w = {};
    if (!host_query(BREAD_OP_GET_WATCHDOG, &reply) || bread_watchdog_parse_payload(reply.data, reply.data_len, &w) != 0)
        host_check("GET_WATCHDOG answers", false);
    return w;
}

static bool relays_off()
{
#if (RLHT_RELAY_COUNT >= 2)
    return host_pin_level[RELAY1] == LOW && host_pin_level[RELAY2] == LOW;
#else
    return host_pin_level[RELAY1] == LOW;
#endif
}

int main()
{
    setup();
    loop();

    mode(RLHT_MODE_OPEN_LOOP);
    open_duty(100, 100);
    loop();
    host_check("open loop heats before the trip", host_pin_level[RELAY1] == HIGH);

    arm(100);
    host_now += 99;
    loop(); // the e-stop poll's read adds 1 ms: 100 ms since the arm
    bread_watchdog_result_t w = wdog();
    host_check("silence for the timeout trips the watchdog", w.armed == 1 && w.timeout_ms == 100 && w.tripped == 1 &&
                                                                 w.trip_count == 1);
    host_check("a trip turns the relays off", relays_off());
    host_check("a trip zeroes the on-times", state().on1_ms == 0);

    open_duty(100, 100);
    crumbs_ack_t a = host_read_ack();
    host_check("SET_OPEN_DUTY while tripped is refused WATCHDOG_TRIP",
               a.status == CRUMBS_ACK_REFUSED && a.reason == BREAD_REFUSE_WATCHDOG_TRIP);

    arm(1000);
    host_check("re-arming leaves the trip latched", wdog().tripped == 1);

    host_send(RLHT_TYPE_ID, BREAD_OP_CLEAR_WATCHDOG_TRIP, nullptr, 0);
    a = host_read_ack();
    host_check("CLEAR_WATCHDOG_TRIP is applied", a.status == CRUMBS_ACK_APPLIED);
    w = wdog();
    host_check("the clear lifts the trip and keeps the count", w.tripped == 0 && w.trip_count == 1 && w.timeout_ms == 1000);
    loop();
    host_check("the clear heats nothing", relays_off() && state().on1_ms == 0);

    return host_finish();
}
