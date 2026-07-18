#include <Arduino.h>
#include <math.h>

#include <crumbs.h>
#include <crumbs_message_helpers.h>

#include <bread/rlht_ops.h>

#include "config.h"
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
    uint8_t mode = RLHT_MODE_CLOSED_LOOP;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (crumbs_msg_read_u8(data, data_len, 0, &mode) != 0)
        return;

    if (mode != RLHT_MODE_CLOSED_LOOP && mode != RLHT_MODE_OPEN_LOOP)
        return;

    slice.mode = static_cast<ControlMode>(mode);
}

void handler_set_setpoints(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data)
{
    int16_t sp1_deci = 0;
    int16_t sp2_deci = 0;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (crumbs_msg_read_i16(data, data_len, 0, &sp1_deci) != 0)
        return;
    if (crumbs_msg_read_i16(data, data_len, 2, &sp2_deci) != 0)
        return;

    slice.relayHeater1.setpointTemperature = deci_c_to_temp(sp1_deci);
    slice.relayHeater2.setpointTemperature = deci_c_to_temp(sp2_deci);
}

void handler_set_pid(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data)
{
    uint8_t kp1_x10 = 0;
    uint8_t ki1_x10 = 0;
    uint8_t kd1_x10 = 0;
    uint8_t kp2_x10 = 0;
    uint8_t ki2_x10 = 0;
    uint8_t kd2_x10 = 0;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (crumbs_msg_read_u8(data, data_len, 0, &kp1_x10) != 0)
        return;
    if (crumbs_msg_read_u8(data, data_len, 1, &ki1_x10) != 0)
        return;
    if (crumbs_msg_read_u8(data, data_len, 2, &kd1_x10) != 0)
        return;
    if (crumbs_msg_read_u8(data, data_len, 3, &kp2_x10) != 0)
        return;
    if (crumbs_msg_read_u8(data, data_len, 4, &ki2_x10) != 0)
        return;
    if (crumbs_msg_read_u8(data, data_len, 5, &kd2_x10) != 0)
        return;

    slice.relayHeater1.Kp = ((double)kp1_x10) / 10.0;
    slice.relayHeater1.Ki = ((double)ki1_x10) / 10.0;
    slice.relayHeater1.Kd = ((double)kd1_x10) / 10.0;
    slice.relayHeater2.Kp = ((double)kp2_x10) / 10.0;
    slice.relayHeater2.Ki = ((double)ki2_x10) / 10.0;
    slice.relayHeater2.Kd = ((double)kd2_x10) / 10.0;
}

void handler_set_periods(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data)
{
    uint16_t p1 = 1000;
    uint16_t p2 = 1000;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (crumbs_msg_read_u16(data, data_len, 0, &p1) != 0)
        return;
    if (crumbs_msg_read_u16(data, data_len, 2, &p2) != 0)
        return;

    setRelayPeriod(1, p1);
    setRelayPeriod(2, p2);
}

void handler_set_tc_select(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data)
{
    uint8_t tc1 = 0;
    uint8_t tc2 = 0;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (crumbs_msg_read_u8(data, data_len, 0, &tc1) != 0)
        return;
    if (crumbs_msg_read_u8(data, data_len, 1, &tc2) != 0)
        return;

    if (tc1 == 1 || tc1 == 2)
        slice.relayHeater1.thermocoupleSelect = tc1;
    if (tc2 == 1 || tc2 == 2)
        slice.relayHeater2.thermocoupleSelect = tc2;
}

void handler_set_open_duty(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data)
{
    uint8_t d1 = 0;
    uint8_t d2 = 0;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (crumbs_msg_read_u8(data, data_len, 0, &d1) != 0)
        return;
    if (crumbs_msg_read_u8(data, data_len, 1, &d2) != 0)
        return;

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
    crumbs_build_version_reply(reply, RLHT_TYPE_ID, RLHT_MODULE_VER_MAJOR, RLHT_MODULE_VER_MINOR, RLHT_MODULE_VER_PATCH);
}

void reply_get_state(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data)
{
    uint8_t flags = 0;
    uint8_t tc_pack = 0;
    uint16_t on1 = 0;
    uint16_t on2 = 0;
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
    on2d = slice.relayHeater2.relayOnTime;

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

    crumbs_msg_init(reply, RLHT_TYPE_ID, RLHT_OP_GET_STATE);
    crumbs_msg_add_u8(reply, (uint8_t)slice.mode);
    crumbs_msg_add_u8(reply, flags);
    crumbs_msg_add_i16(reply, temp_to_deci_c(slice.temperature1));
    crumbs_msg_add_i16(reply, temp_to_deci_c(slice.temperature2));
    crumbs_msg_add_i16(reply, temp_to_deci_c(slice.relayHeater1.setpointTemperature));
    crumbs_msg_add_i16(reply, temp_to_deci_c(slice.relayHeater2.setpointTemperature));
    crumbs_msg_add_u16(reply, on1);
    crumbs_msg_add_u16(reply, on2);
    crumbs_msg_add_u16(reply, (uint16_t)slice.relayHeater1.relayPeriod);
    crumbs_msg_add_u16(reply, (uint16_t)slice.relayHeater2.relayPeriod);
    crumbs_msg_add_u8(reply, tc_pack);
}

void reply_get_caps(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data)
{
    (void)ctx;
    (void)user_data;

    (void)bread_caps_build_reply(reply, RLHT_TYPE_ID, RLHT_CAP_LEVEL_1,
                                 RLHT_CAP_BASELINE_FLAGS | RLHT_CAP_CMD_WATCHDOG);
}
