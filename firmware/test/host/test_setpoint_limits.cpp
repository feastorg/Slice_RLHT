// SET_SETPOINTS is refused WRONG_MODE outside closed loop and
// UNDEFINED_VALUE above RLHT_SETPOINT_MAX_C, storing neither setpoint
// (#47, RLHT_CAP_SETPOINT_LIMITS). Before, any i16 was stored in either
// mode, and one sent in open loop heated at the next switch to closed loop.
#include "host.h"
#include "globals.h"
#include <bread/bread_caps.h>
#include <bread/rlht_ops.h>

void setup();
void loop();

static void send_setpoints(int16_t sp1_deci_c, int16_t sp2_deci_c)
{
    const uint8_t p[4] = {(uint8_t)(sp1_deci_c & 0xFF), (uint8_t)((uint16_t)sp1_deci_c >> 8),
                          (uint8_t)(sp2_deci_c & 0xFF), (uint8_t)((uint16_t)sp2_deci_c >> 8)};
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_SETPOINTS, p, 4);
}

static void send_mode(uint8_t mode)
{
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_MODE, &mode, 1);
}

static bool refused(const crumbs_ack_t &a, uint8_t reason)
{
    return a.status == CRUMBS_ACK_REFUSED && a.reason == reason;
}

static bool setpoints_are(double sp1, double sp2)
{
    return slice.relayHeater1.setpointTemperature == sp1 && slice.relayHeater2.setpointTemperature == sp2;
}

int main()
{
    setup();
    loop();

    bread_caps_result_t caps;
    crumbs_message_t reply;
    host_check("GET_CAPS advertises RLHT_CAP_SETPOINT_LIMITS",
               host_query(BREAD_OP_GET_CAPS, &reply) && bread_caps_parse_payload(reply.data, reply.data_len, &caps) == 0 &&
                   (caps.flags & RLHT_CAP_SETPOINT_LIMITS) != 0);

    // Boots in closed loop.
    send_setpoints(400, 600);
    host_check("40.0 and 60.0 C in closed loop are applied",
               host_read_ack().status == CRUMBS_ACK_APPLIED && setpoints_are(40.0, 60.0));

    send_setpoints(RLHT_SETPOINT_MAX_C * 10, 0);
    host_check("the ceiling itself is applied",
               host_read_ack().status == CRUMBS_ACK_APPLIED && setpoints_are(RLHT_SETPOINT_MAX_C, 0.0));

    send_setpoints(400, 600);
    (void)host_read_ack();
    send_setpoints(RLHT_SETPOINT_MAX_C * 10 + 1, 100);
    host_check("0.1 C over the ceiling on heater 1 is refused UNDEFINED_VALUE",
               refused(host_read_ack(), BREAD_REFUSE_UNDEFINED_VALUE));
    host_check("and stores neither setpoint", setpoints_are(40.0, 60.0));

    send_setpoints(100, 6000);
    host_check("600 C on heater 2 is refused UNDEFINED_VALUE and stores neither",
               refused(host_read_ack(), BREAD_REFUSE_UNDEFINED_VALUE) && setpoints_are(40.0, 60.0));

    send_setpoints(32767, 32767);
    host_check("the i16 maximum is refused UNDEFINED_VALUE",
               refused(host_read_ack(), BREAD_REFUSE_UNDEFINED_VALUE) && setpoints_are(40.0, 60.0));

    send_setpoints(-50, 0);
    host_check("a negative setpoint is off and applied",
               host_read_ack().status == CRUMBS_ACK_APPLIED && setpoints_are(-5.0, 0.0));

    send_setpoints(0, 0);
    (void)host_read_ack();
    send_mode(RLHT_MODE_OPEN_LOOP);
    (void)host_read_ack();
    send_setpoints(400, 400);
    host_check("SET_SETPOINTS in open loop is refused WRONG_MODE",
               refused(host_read_ack(), BREAD_REFUSE_WRONG_MODE) && setpoints_are(0.0, 0.0));

    // The refused setpoint must not heat after the switch back.
    send_mode(RLHT_MODE_CLOSED_LOOP);
    (void)host_read_ack();
    host_tc1_celsius = 20.0f;
    host_tc2_celsius = 20.0f;
    for (int i = 0; i < 5; i++)
    {
        host_now += 1000;
        loop();
    }
    host_check("back in closed loop, nothing heats from it",
               setpoints_are(0.0, 0.0) && slice.relayHeater1.relayOnTime == 0 && slice.relayHeater2.relayOnTime == 0);

    return host_finish();
}
