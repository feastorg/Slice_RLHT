// SET_PID is refused unless its payload is exactly rlht_set_pid_wire_size
// bytes (#50). A widened SET_PID (u16 x1000, 12 bytes, as
// bread-crumbs-contracts#64 proposes) would otherwise be read as u8 x10
// gains and applied.
#include "host.h"
#include "globals.h"
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

    // Kp 1.5, Ki 0.2, Kd 0.1 on both heaters, as u8 x10.
    const uint8_t six[6] = {15, 2, 1, 15, 2, 1};
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_PID, six, 6);
    host_check("a 6-byte SET_PID is applied", host_read_ack().status == CRUMBS_ACK_APPLIED && gains_are(1.5, 0.2, 0.1));

    // The same gains as u16 x1000, little-endian: 12 bytes.
    const uint8_t twelve[12] = {0xDC, 0x05, 0xC8, 0x00, 0x64, 0x00, 0xDC, 0x05, 0xC8, 0x00, 0x64, 0x00};
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_PID, twelve, 12);
    host_check("a 12-byte SET_PID is refused MALFORMED", refused_malformed(host_read_ack()));
    host_check("a 12-byte SET_PID changes no gain", gains_are(1.5, 0.2, 0.1));

    const uint8_t seven[7] = {50, 0, 0, 50, 0, 0, 0};
    host_send(RLHT_TYPE_ID, RLHT_OP_SET_PID, seven, 7);
    host_check("a 7-byte SET_PID is refused MALFORMED", refused_malformed(host_read_ack()) && gains_are(1.5, 0.2, 0.1));

    host_send(RLHT_TYPE_ID, RLHT_OP_SET_PID, six, 5);
    host_check("a 5-byte SET_PID is refused MALFORMED", refused_malformed(host_read_ack()) && gains_are(1.5, 0.2, 0.1));

    return host_finish();
}
