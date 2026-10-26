// SET_PID takes two lengths and refuses any other (#50): 6 bytes, six u8
// gains x10, and 12 bytes, six u16 gains x1000 (RLHT_CAP_PID_X1000,
// bread-crumbs-contracts#64). Before the 12-byte form, a 12-byte payload
// would have been read as u8 x10 gains and applied.
#include "host.h"
#include "globals.h"
#include <bread/bread_caps.h>
#include <bread/rlht_ops.h>

void setup();
void loop();

static bool gains_are(double kp, double ki, double kd)
{
    return slice.relayHeater1.Kp == kp && slice.relayHeater1.Ki == ki && slice.relayHeater1.Kd == kd &&
           slice.relayHeater2.Kp == kp && slice.relayHeater2.Ki == ki && slice.relayHeater2.Kd == kd;
}

static bool refused_malformed(const crumbs_ack_t &a)
{
    return a.status == CRUMBS_ACK_REFUSED && a.reason == BREAD_REFUSE_MALFORMED;
}

int main()
{
    setup();
    loop();

    crumbs_message_t reply;
    bread_caps_result_t caps;
    host_check("GET_CAPS advertises RLHT_CAP_PID_X1000",
               host_query(BREAD_OP_GET_CAPS, &reply) && bread_caps_parse_payload(reply.data, reply.data_len, &caps) == 0 &&
                   (caps.flags & RLHT_CAP_PID_X1000) != 0);

    // Kp 1.5, Ki 0.2, Kd 0.1 on both heaters, as u8 x10.
    const uint8_t six[6] = {15, 2, 1, 15, 2, 1};
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_PID, six, 6);
    host_check("a 6-byte SET_PID is applied", host_read_ack().status == CRUMBS_ACK_APPLIED && gains_are(1.5, 0.2, 0.1));

    // Kp 50.000, Ki 0.125, Kd 1.003 as u16 x1000, little-endian: 12 bytes.
    // None of these is reachable through the 6-byte form.
    const uint8_t twelve[12] = {0x50, 0xC3, 0x7D, 0x00, 0xEB, 0x03, 0x50, 0xC3, 0x7D, 0x00, 0xEB, 0x03};
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_PID, twelve, 12);
    host_check("a 12-byte SET_PID is applied as u16 x1000",
               host_read_ack().status == CRUMBS_ACK_APPLIED && gains_are(50.0, 0.125, 1.003));

    const uint8_t max12[12] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_PID, max12, 12);
    host_check("the u16 maximum is 65.535",
               host_read_ack().status == CRUMBS_ACK_APPLIED && gains_are(65.535, 65.535, 65.535));

    host_send(RLHT_TYPE_ID, RLHT_OP_SET_PID, six, 6);
    (void)host_read_ack();

    const uint8_t seven[7] = {50, 0, 0, 50, 0, 0, 0};
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_PID, seven, 7);
    host_check("a 7-byte SET_PID is refused MALFORMED", refused_malformed(host_read_ack()) && gains_are(1.5, 0.2, 0.1));

    host_send(RLHT_TYPE_ID, RLHT_OP_SET_PID, six, 5);
    host_check("a 5-byte SET_PID is refused MALFORMED", refused_malformed(host_read_ack()) && gains_are(1.5, 0.2, 0.1));

    const uint8_t thirteen[13] = {0x50, 0xC3, 0, 0, 0, 0, 0x50, 0xC3, 0, 0, 0, 0, 0};
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_PID, thirteen, 13);
    host_check("a 13-byte SET_PID is refused MALFORMED", refused_malformed(host_read_ack()) && gains_are(1.5, 0.2, 0.1));

    host_send(RLHT_TYPE_ID, RLHT_OP_SET_PID, twelve, 11);
    host_check("an 11-byte SET_PID is refused MALFORMED", refused_malformed(host_read_ack()) && gains_are(1.5, 0.2, 0.1));

    return host_finish();
}
