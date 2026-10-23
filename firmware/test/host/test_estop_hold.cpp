// An e-stop press turns the relays off, zeroes the on-times, and GET_STATE
// reports it; commands are refused while it is held, and a release heats
// nothing (RLHT_CAP_ESTOP_HOLD). The I2C half of a bench e-stop step: press
// during open-loop drive, a refused SET, release, press again.
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

static rlht_state_result_t state()
{
    crumbs_message_t reply;
    rlht_state_result_t s = {};
    if (!host_query(RLHT_OP_GET_STATE, &reply) || rlht_parse_state_payload(reply.data, reply.data_len, &s) != 0)
        host_check("GET_STATE answers", false);
    return s;
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
    rlht_state_result_t s = state();
    host_check("open loop heats before the press",
               host_pin_level[RELAY1] == HIGH && s.on1_ms > 0 && !(s.flags & RLHT_FLAG_ESTOP) && s.estop_count == 0);

    host_estop_level = LOW;
    loop();
    s = state();
    host_check("a press is reported in GET_STATE", (s.flags & RLHT_FLAG_ESTOP) && s.estop_count == 1);
    host_check("a press turns the relays off", relays_off());
    host_check("a press zeroes the on-times", s.on1_ms == 0 && s.on2_ms == 0);

    open_duty(100, 100);
    const crumbs_ack_t a = host_read_ack();
    host_check("SET_OPEN_DUTY while held is refused ESTOP",
               a.status == CRUMBS_ACK_REFUSED && a.reason == BREAD_REFUSE_ESTOP);

    loop();
    loop();
    host_check("a held line counts one press", state().estop_count == 1);

    // Release: the first high sample starts the 25 ms debounce.
    host_estop_level = HIGH;
    loop();
    host_check("the release is debounced", state().flags & RLHT_FLAG_ESTOP);
    host_now += 30;
    loop();
    s = state();
    host_check("the release is reported", !(s.flags & RLHT_FLAG_ESTOP));
    loop();
    host_check("the release heats nothing", relays_off() && state().on1_ms == 0);

    host_estop_level = LOW;
    loop();
    host_check("a second press counts", state().estop_count == 2);

    return host_finish();
}
