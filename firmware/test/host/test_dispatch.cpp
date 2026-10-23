// Which frames the firmware takes as commands (#36): the real setup()
// against the real CRUMBS core, every opcode sent as a command frame, and the
// result read back through GET_ACK.
#include "host.h"
#include "globals.h"
#include <bread/dcmt_ops.h>
#include <bread/rlht_ops.h>
#include <cstdio>

void setup();
void loop();

static const uint8_t COMMANDS[] = {
    RLHT_OP_SET_MODE,    RLHT_OP_SET_SETPOINTS, RLHT_OP_SET_PID,       RLHT_OP_SET_PERIODS,
    RLHT_OP_SET_TC_SELECT, RLHT_OP_SET_OPEN_DUTY, BREAD_OP_SET_WATCHDOG, BREAD_OP_CLEAR_WATCHDOG_TRIP,
};
static const uint8_t QUERIES[] = {RLHT_OP_GET_STATE, BREAD_OP_GET_WATCHDOG, BREAD_OP_GET_CAPS};

static bool is_command(uint8_t op)
{
    for (uint8_t c : COMMANDS)
        if (c == op)
            return true;
    return false;
}

static bool refused_unknown(const crumbs_ack_t &a)
{
    return a.status == CRUMBS_ACK_REFUSED && a.reason == BREAD_REFUSE_UNKNOWN_OP;
}

static void set_watchdog(uint8_t type_id, uint16_t ms)
{
    const uint8_t d[2] = {(uint8_t)(ms & 0xFF), (uint8_t)(ms >> 8)};
    host_send(type_id, BREAD_OP_SET_WATCHDOG, d, 2);
}

int main()
{
    setup();
    host_check("setup() initialises CRUMBS", host_ctx != nullptr);
    if (!host_ctx)
        return host_finish();

    // Every opcode but SET_REPLY, as a command of RLHT's type with a 6-byte
    // zero payload. A command is never refused UNKNOWN_OP; anything else is.
    const uint8_t zeros[CRUMBS_MAX_PAYLOAD] = {0};
    int wrong_cmd = 0, wrong_other = 0;
    for (int op = 0; op <= 0xFF; op++)
    {
        if (op == CRUMBS_CMD_SET_REPLY)
            continue;
        host_send(RLHT_TYPE_ID, (uint8_t)op, zeros, 6);
        const crumbs_ack_t a = host_read_ack();
        if (is_command((uint8_t)op) && refused_unknown(a))
        {
            printf("     0x%02X: command refused UNKNOWN_OP\n", op);
            wrong_cmd++;
        }
        if (!is_command((uint8_t)op) && !refused_unknown(a))
        {
            printf("     0x%02X: status %u reason 0x%02X, want refused UNKNOWN_OP\n", op, a.status, a.reason);
            wrong_other++;
        }
    }
    host_check("no command opcode is refused UNKNOWN_OP", wrong_cmd == 0);
    host_check("every other opcode is refused UNKNOWN_OP", wrong_other == 0);

    char what[80];
    for (uint8_t q : QUERIES)
    {
        host_send(RLHT_TYPE_ID, q, nullptr, 0);
        snprintf(what, sizeof what, "GET 0x%02X sent as a command is refused UNKNOWN_OP", q);
        host_check(what, refused_unknown(host_read_ack()));

        crumbs_message_t reply;
        snprintf(what, sizeof what, "GET 0x%02X through SET_REPLY answers with that opcode", q);
        host_check(what, host_query(q, &reply) && reply.opcode == q);
    }

    // SET_WATCHDOG 0 disarms, so it is safe to apply on any build.
    set_watchdog(RLHT_TYPE_ID, 0);
    host_check("a command of RLHT's type is applied", host_read_ack().status == CRUMBS_ACK_APPLIED);
    crumbs_message_t ack;
    set_watchdog(RLHT_TYPE_ID, 0);
    host_check("GET_ACK replies carry RLHT's type", host_query(CRUMBS_CMD_GET_ACK, &ack) && ack.type_id == RLHT_TYPE_ID);
    set_watchdog(CRUMBS_TYPE_ID_ANY, 0);
    host_check("a command of type 0x00 is applied", host_read_ack().status == CRUMBS_ACK_APPLIED);

    // Another family's frame is rejected before any handler runs: it neither
    // arms the watchdog nor counts as bus liveness. With a 1000 ms timeout
    // armed, DCMT's SET_WATCHDOG 500 arrives 900 ms in; the trip still comes
    // at 1000 ms, where a refresh would have moved it to 1900 ms.
    set_watchdog(RLHT_TYPE_ID, 1000);
    host_now += 900;
    set_watchdog(DCMT_TYPE_ID, 500);
    const crumbs_ack_t a = host_read_ack();
    host_check("another family's frame is rejected REJECT_TYPE",
               a.status == CRUMBS_ACK_REJECTED && a.reason == CRUMBS_ACK_REJECT_TYPE);
    host_now += 99;
    loop(); // the e-stop poll's read adds 1 ms: 1000 ms since the arm
    crumbs_message_t reply;
    bread_watchdog_result_t w = {};
    host_check("GET_WATCHDOG answers", host_query(BREAD_OP_GET_WATCHDOG, &reply) &&
                                           bread_watchdog_parse_payload(reply.data, reply.data_len, &w) == 0);
    host_check("another family's frame does not arm the watchdog", w.timeout_ms == 1000);
    host_check("another family's frame does not refresh the watchdog", w.tripped == 1);

    return host_finish();
}
