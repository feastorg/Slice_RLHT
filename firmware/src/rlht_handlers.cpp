#include <Arduino.h>
#include <math.h>

#include <crumbs.h>
#include <crumbs_message_helpers.h>

#include <bread/rlht_ops.h>

#include "config.h"
#include "config_hardware.h"
#include "globals.h"

static int16_t temp_to_deci_c(double t)
{
    if (isnan(t))
        return BREAD_INVALID_I16;

    long scaled = lround(t * 10.0);
    if (scaled > 32767)
        scaled = 32767;
    if (scaled < -32768)
        scaled = -32768;
    return (int16_t)scaled;
}

static double deci_c_to_temp(int16_t t)
{
    return ((double)t) / 10.0;
}

void handler_set_watchdog(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data)
{
    uint16_t timeout_ms = 0;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (crumbs_msg_read_u16(data, data_len, 0, &timeout_ms) != 0)
        return;

    wdTimeoutMs = timeout_ms;
    wdLastRxMs = millis();
    wdTripped = false;
}

void reply_get_watchdog(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data)
{
    uint16_t timeout_ms = wdTimeoutMs;
    (void)ctx;
    (void)user_data;

    (void)bread_watchdog_build_reply(reply, RLHT_TYPE_ID,
                                     timeout_ms != 0 ? 1 : 0, timeout_ms,
                                     wdTripped ? 1 : 0, wdTripCount);
    // A reply build proves a live master too.
    wdLastRxMs = millis();
}

void handler_set_mode(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data)
{
    rlht_set_mode_t v;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (rlht_set_mode_unpack(data, data_len, &v) != 0)
        return;

    if (v.mode != RLHT_MODE_CLOSED_LOOP && v.mode != RLHT_MODE_OPEN_LOOP)
        return;

    // Entering open loop starts from 0, not from the PID's last on-time,
    // which open loop would otherwise keep driving with no setpoint to stop it.
    if (v.mode == RLHT_MODE_OPEN_LOOP && slice.mode != OPEN_LOOP)
    {
        slice.relayHeater1.relayOnTime = 0;
        slice.relayHeater2.relayOnTime = 0;
    }
    slice.mode = static_cast<ControlMode>(v.mode);
}

void handler_set_setpoints(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data)
{
    rlht_set_setpoints_t v;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (rlht_set_setpoints_unpack(data, data_len, &v) != 0)
        return;

    slice.relayHeater1.setpointTemperature = deci_c_to_temp(v.sp1_deci_c);
    slice.relayHeater2.setpointTemperature = deci_c_to_temp(v.sp2_deci_c);
}

void handler_set_pid(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data)
{
    rlht_set_pid_t v;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (rlht_set_pid_unpack(data, data_len, &v) != 0)
        return;

    slice.relayHeater1.Kp = ((double)v.kp1_x10) / 10.0;
    slice.relayHeater1.Ki = ((double)v.ki1_x10) / 10.0;
    slice.relayHeater1.Kd = ((double)v.kd1_x10) / 10.0;
    slice.relayHeater2.Kp = ((double)v.kp2_x10) / 10.0;
    slice.relayHeater2.Ki = ((double)v.ki2_x10) / 10.0;
    slice.relayHeater2.Kd = ((double)v.kd2_x10) / 10.0;
}

void handler_set_periods(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data)
{
    rlht_set_periods_t v;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (rlht_set_periods_unpack(data, data_len, &v) != 0)
        return;

    setRelayPeriod(1, v.p1_ms);
    setRelayPeriod(2, v.p2_ms);
}

void handler_set_tc_select(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data)
{
    rlht_set_tc_select_t v;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (rlht_set_tc_select_unpack(data, data_len, &v) != 0)
        return;

    if (v.tc1 == 1 || v.tc1 == 2)
        slice.relayHeater1.thermocoupleSelect = v.tc1;
    if (v.tc2 == 1 || v.tc2 == 2)
        slice.relayHeater2.thermocoupleSelect = v.tc2;
}

void handler_set_open_duty(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data)
{
    rlht_set_open_duty_t v;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (rlht_set_open_duty_unpack(data, data_len, &v) != 0)
        return;

    uint8_t d1 = v.duty1_pct;
    uint8_t d2 = v.duty2_pct;

    if (slice.mode != OPEN_LOOP)
        return;

    if (d1 > 100)
        d1 = 100;
    if (d2 > 100)
        d2 = 100;

    slice.relayHeater1.relayOnTime = map(d1, 0, 100, 0, slice.relayHeater1.relayPeriod);
    slice.relayHeater2.relayOnTime = map(d2, 0, 100, 0, slice.relayHeater2.relayPeriod);
}

void reply_version(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data)
{
    (void)ctx;
    (void)user_data;
    // Every reply build proves a live master (SET_REPLY staging frames are
    // not dispatched to on_message, so this is where poll traffic stamps).
    wdLastRxMs = millis();
    crumbs_build_version_reply(reply, RLHT_TYPE_ID, RLHT_MODULE_VER_MAJOR, RLHT_MODULE_VER_MINOR, RLHT_MODULE_VER_PATCH);
}

void reply_get_state(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data)
{
    rlht_state_t s;
    uint8_t flags = 0;
    uint8_t tc_pack = 0;
    uint16_t on1 = 0;
    uint16_t on2 = 0;

    // The controller's periodic state poll is the primary liveness signal.
    wdLastRxMs = millis();
    double on1d = 0.0;
    double on2d = 0.0;
    (void)ctx;
    (void)user_data;

    if (slice.eStop)
        flags |= RLHT_FLAG_ESTOP;
    if (slice.relay1State)
        flags |= RLHT_FLAG_RELAY1_ON;
    if (slice.relay2State)
        flags |= RLHT_FLAG_RELAY2_ON;

    on1d = slice.relayHeater1.relayOnTime;
#if (RLHT_RELAY_COUNT >= 2)
    // Gen1 accepts channel-2 commands but has no relay to run them.
    on2d = slice.relayHeater2.relayOnTime;
#endif

    // Defensive clamp for reply serialization only. Runtime invariants should
    // keep values in-range via PID output limits and command validation.
    if (on1d < 0.0)
        on1d = 0.0;
    if (on2d < 0.0)
        on2d = 0.0;
    if (on1d > (double)slice.relayHeater1.relayPeriod)
        on1d = (double)slice.relayHeater1.relayPeriod;
    if (on2d > (double)slice.relayHeater2.relayPeriod)
        on2d = (double)slice.relayHeater2.relayPeriod;

    on1 = (uint16_t)on1d;
    on2 = (uint16_t)on2d;

    tc_pack = (slice.relayHeater1.thermocoupleSelect & 0x03);
    tc_pack |= (uint8_t)((slice.relayHeater2.thermocoupleSelect & 0x03) << 2);

    s.mode = (uint8_t)slice.mode;
    s.flags = flags;
    s.t1_deci_c = temp_to_deci_c(slice.temperature1);
    s.t2_deci_c = temp_to_deci_c(slice.temperature2);
    s.sp1_deci_c = temp_to_deci_c(slice.relayHeater1.setpointTemperature);
    s.sp2_deci_c = temp_to_deci_c(slice.relayHeater2.setpointTemperature);
    s.on1_ms = on1;
    s.on2_ms = on2;
    s.period1_ms = (uint16_t)slice.relayHeater1.relayPeriod;
    s.period2_ms = (uint16_t)slice.relayHeater2.relayPeriod;
    s.tc_select = tc_pack;

    crumbs_msg_init(reply, RLHT_TYPE_ID, RLHT_OP_GET_STATE);
    (void)rlht_state_pack(reply, &s);
}

void reply_get_caps(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data)
{
    (void)ctx;
    (void)user_data;

    wdLastRxMs = millis();
    (void)bread_caps_build_reply(reply, RLHT_TYPE_ID, RLHT_CAP_LEVEL_1,
                                 RLHT_CAP_BASELINE_FLAGS | RLHT_CAP_CMD_WATCHDOG);
}
