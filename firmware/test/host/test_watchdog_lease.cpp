// The command lease (RLHT_CAP_WATCHDOG_LEASE, bread-crumbs-contracts#63): a
// 3-byte SET_WATCHDOG with BREAD_WATCHDOG_FLAG_COMMANDS_ONLY lets only
// SET_MODE, SET_SETPOINTS, SET_OPEN_DUTY and SET_WATCHDOG refresh the
// watchdog, so a poller that outlives the control path no longer keeps the
// heaters on. The 2-byte form and serial WDOG= return to the default, where
// every valid frame, reply and serial line refreshes it.
#include "host.h"
#include "globals.h"
#include <bread/bread_caps.h>
#include <bread/rlht_ops.h>

void setup();
void loop();

static void set_watchdog2(uint16_t ms)
{
    const uint8_t d[2] = {(uint8_t)(ms & 0xFF), (uint8_t)(ms >> 8)};
    host_send(RLHT_TYPE_ID, BREAD_OP_SET_WATCHDOG, d, 2);
}

static void set_watchdog3(uint16_t ms, uint8_t flags)
{
    const uint8_t d[3] = {(uint8_t)(ms & 0xFF), (uint8_t)(ms >> 8), flags};
    host_send(RLHT_TYPE_ID, BREAD_OP_SET_WATCHDOG, d, 3);
}

static bool applied() { return host_read_ack().status == CRUMBS_ACK_APPLIED; }

static void poll_state()
{
    crumbs_message_t reply;
    (void)host_query(RLHT_OP_GET_STATE, &reply);
}

// Steps 100 ms at a time for `ms`, calling `each` before every pass.
template <typename F> static void run_for(unsigned long ms, F each)
{
    for (unsigned long t = 0; t < ms; t += 100)
    {
        host_now += 100;
        each();
        loop();
    }
}

// Clears a trip, disarms, and leaves the default flags.
static void reset()
{
    host_send(RLHT_TYPE_ID, BREAD_OP_CLEAR_WATCHDOG_TRIP, nullptr, 0);
    (void)host_read_ack();
    set_watchdog2(0);
    (void)host_read_ack();
    loop();
}

int main()
{
    setup();
    loop();

    crumbs_message_t reply;
    bread_caps_result_t caps;
    host_check("GET_CAPS advertises RLHT_CAP_WATCHDOG_LEASE",
               host_query(BREAD_OP_GET_CAPS, &reply) && bread_caps_parse_payload(reply.data, reply.data_len, &caps) == 0 &&
                   (caps.flags & RLHT_CAP_WATCHDOG_LEASE) != 0);

    // Default: polls alone keep the watchdog alive.
    set_watchdog2(500);
    host_check("the 2-byte form is applied", applied());
    run_for(1500, poll_state);
    host_check("by default a GET_STATE poll keeps it alive", !commandWatchdog.tripped());
    reset();

    // Commands-only: polls do not.
    set_watchdog3(500, BREAD_WATCHDOG_FLAG_COMMANDS_ONLY);
    host_check("the lease form is applied", applied() && commandWatchdog.timeoutMs() == 500);
    run_for(1500, poll_state);
    host_check("with a commands-only lease a poll alone lets it trip", commandWatchdog.tripped());
    reset();

    // Commands-only: a non-actuating command does not either.
    set_watchdog3(500, BREAD_WATCHDOG_FLAG_COMMANDS_ONLY);
    (void)applied();
    const uint8_t pid[6] = {10, 0, 0, 10, 0, 0};
    run_for(1500, [&] { host_send(RLHT_TYPE_ID, RLHT_OP_SET_PID, pid, 6); });
    host_check("SET_PID does not refresh a commands-only lease", commandWatchdog.tripped());
    reset();

    // Commands-only: an actuating command does.
    set_watchdog3(500, BREAD_WATCHDOG_FLAG_COMMANDS_ONLY);
    (void)applied();
    const uint8_t closed = RLHT_MODE_CLOSED_LOOP;
    run_for(1500, [&] {
        poll_state();
        host_send(RLHT_TYPE_ID, RLHT_OP_SET_MODE, &closed, 1);
    });
    host_check("SET_MODE refreshes a commands-only lease", !commandWatchdog.tripped());

    // Re-sending SET_WATCHDOG is the keepalive.
    run_for(1500, [&] { set_watchdog3(500, BREAD_WATCHDOG_FLAG_COMMANDS_ONLY); });
    host_check("re-sending the lease keeps it alive", !commandWatchdog.tripped());

    // Serial lines do not refresh a commands-only lease.
    run_for(1500, [&] { Serial.in += "HELP\n"; });
    host_check("a serial line does not refresh a commands-only lease", commandWatchdog.tripped());
    reset();

    // The 2-byte form ends the lease.
    set_watchdog3(500, BREAD_WATCHDOG_FLAG_COMMANDS_ONLY);
    (void)applied();
    set_watchdog2(500);
    (void)applied();
    run_for(1500, poll_state);
    host_check("the 2-byte form returns polls to refreshing it", !commandWatchdog.tripped());
    reset();

    // So does serial WDOG=.
    set_watchdog3(500, BREAD_WATCHDOG_FLAG_COMMANDS_ONLY);
    (void)applied();
    Serial.in += "WDOG=500\n";
    loop();
    run_for(1500, poll_state);
    host_check("serial WDOG= ends the lease too", !commandWatchdog.tripped());
    reset();

    // A flag the firmware does not define is refused and arms nothing.
    set_watchdog3(500, 0x02);
    const crumbs_ack_t a = host_read_ack();
    host_check("an undefined flag is refused UNDEFINED_VALUE",
               a.status == CRUMBS_ACK_REFUSED && a.reason == BREAD_REFUSE_UNDEFINED_VALUE);
    host_check("and arms nothing", commandWatchdog.timeoutMs() == 0 && !watchdogCommandsOnly);

    return host_finish();
}
