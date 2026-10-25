// src/serialCommands.cpp
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

#include "globals.h"

// Static receive buffer — no heap allocation.
static char cmdBuf[64];
static uint8_t cmdIdx = 0;

static bool starts_with_P(const char *s, PGM_P prefix)
{
    return strncmp_P(s, prefix, strlen_P(prefix)) == 0;
}

static const char *after_prefix_P(const char *s, PGM_P prefix)
{
    return s + strlen_P(prefix);
}

static void strupper(char *s)
{
    for (; *s; ++s)
        *s = toupper((unsigned char)*s);
}

// Mode, setpoint and on-time commands are ignored while the e-stop is held
// or the watchdog is tripped, as their CRUMBS counterparts are refused:
// nothing sent during a hold is stored and acted on when it ends.
static bool refuse_while_held()
{
    if (estopInput.asserted())
    {
        Serial.println(F("Ignored: e-stop held, release it first"));
        return true;
    }
    if (commandWatchdog.tripped())
    {
        Serial.println(F("Ignored: watchdog tripped, send WDCLEAR first"));
        return true;
    }
    return false;
}

static void processCommand(char *cmd)
{
    // Trim leading/trailing whitespace in-place.
    while (*cmd == ' ' || *cmd == '\t')
        ++cmd;
    if (*cmd == '\0')
        return;
    char *end = cmd + strlen(cmd) - 1;
    while (end > cmd && (*end == ' ' || *end == '\t' || *end == '\r'))
        *end-- = '\0';

    if (*cmd == '\0')
        return;

    // A serial operator is a live master too: feed the command watchdog.
    // Only WDCLEAR clears a trip; any other line, a logger's included,
    // leaves the hold latched.
    commandWatchdog.stamp(millis());

    if (starts_with_P(cmd, PSTR("MODE=")))
    {
        if (refuse_while_held())
            return;
        char *mode = (char *)after_prefix_P(cmd, PSTR("MODE="));
        strupper(mode);
        if (strcmp_P(mode, PSTR("CLOSED_LOOP")) == 0 || strcmp_P(mode, PSTR("0")) == 0)
        {
            slice.mode = CLOSED_LOOP;
            Serial.println(F("Mode set to CLOSED_LOOP"));
        }
        else if (strcmp_P(mode, PSTR("OPEN_LOOP")) == 0 || strcmp_P(mode, PSTR("1")) == 0)
        {
            // As handler_set_mode: entering open loop starts from 0.
            noInterrupts();
            if (slice.mode != OPEN_LOOP)
            {
                slice.relayHeater1.relayOnTime = 0;
                slice.relayHeater2.relayOnTime = 0;
            }
            slice.mode = OPEN_LOOP;
            interrupts();
            Serial.println(F("Mode set to OPEN_LOOP"));
        }
        else
        {
            Serial.println(F("Invalid mode. Use CLOSED_LOOP/0 or OPEN_LOOP/1"));
        }
    }
    else if (starts_with_P(cmd, PSTR("R1TEMP=")))
    {
        if (refuse_while_held())
            return;
        if (slice.mode == CLOSED_LOOP)
        {
            float value;
            if (bsc::parseFloat(after_prefix_P(cmd, PSTR("R1TEMP=")), 0.0f, 500.0f, &value))
            {
                slice.relayHeater1.setpointTemperature = value;
                Serial.print(F("Relay 1 setpoint: "));
                Serial.println(value);
            }
            else
            {
                Serial.println(F("Temp out of range (0-500C)"));
            }
        }
        else
        {
            Serial.println(F("R1 temp only in CLOSED_LOOP"));
        }
    }
    else if (starts_with_P(cmd, PSTR("R2TEMP=")))
    {
        if (refuse_while_held())
            return;
        if (slice.mode == CLOSED_LOOP)
        {
            float value;
            if (bsc::parseFloat(after_prefix_P(cmd, PSTR("R2TEMP=")), 0.0f, 500.0f, &value))
            {
                slice.relayHeater2.setpointTemperature = value;
                Serial.print(F("Relay 2 setpoint: "));
                Serial.println(value);
            }
            else
            {
                Serial.println(F("Temp out of range (0-500C)"));
            }
        }
        else
        {
            Serial.println(F("R2 temp only in CLOSED_LOOP"));
        }
    }
    else if (starts_with_P(cmd, PSTR("R1TIME=")))
    {
        if (refuse_while_held())
            return;
        if (slice.mode == OPEN_LOOP)
        {
            float value;
            if (bsc::parseFloat(after_prefix_P(cmd, PSTR("R1TIME=")), 0.0f, (float)slice.relayHeater1.relayPeriod, &value))
            {
                slice.relayHeater1.relayOnTime = value;
                Serial.print(F("R1 time: "));
                Serial.print(value);
                Serial.println(F("ms"));
            }
            else
            {
                Serial.print(F("Time range 0-"));
                Serial.print(slice.relayHeater1.relayPeriod);
                Serial.println(F("ms"));
            }
        }
        else
        {
            Serial.println(F("R1 time only in OPEN_LOOP"));
        }
    }
    else if (starts_with_P(cmd, PSTR("R2TIME=")))
    {
        if (refuse_while_held())
            return;
        if (slice.mode == OPEN_LOOP)
        {
            float value;
            if (bsc::parseFloat(after_prefix_P(cmd, PSTR("R2TIME=")), 0.0f, (float)slice.relayHeater2.relayPeriod, &value))
            {
                slice.relayHeater2.relayOnTime = value;
                Serial.print(F("R2 time: "));
                Serial.print(value);
                Serial.println(F("ms"));
            }
            else
            {
                Serial.print(F("Time range 0-"));
                Serial.print(slice.relayHeater2.relayPeriod);
                Serial.println(F("ms"));
            }
        }
        else
        {
            Serial.println(F("R2 time only in OPEN_LOOP"));
        }
    }
    else if (starts_with_P(cmd, PSTR("R1TC=")))
    {
        long value;
        if (bsc::parseLong(after_prefix_P(cmd, PSTR("R1TC=")), 1, 2, &value))
        {
            slice.relayHeater1.thermocoupleSelect = (uint8_t)value;
            Serial.print(F("R1 TC: "));
            Serial.println((int)value);
        }
        else
        {
            Serial.println(F("TC select 1 or 2"));
        }
    }
    else if (starts_with_P(cmd, PSTR("R2TC=")))
    {
        long value;
        if (bsc::parseLong(after_prefix_P(cmd, PSTR("R2TC=")), 1, 2, &value))
        {
            slice.relayHeater2.thermocoupleSelect = (uint8_t)value;
            Serial.print(F("R2 TC: "));
            Serial.println((int)value);
        }
        else
        {
            Serial.println(F("TC select 1 or 2"));
        }
    }
    else if (starts_with_P(cmd, PSTR("R1KP=")))
    {
        float value;
        if (bsc::parseFloat(after_prefix_P(cmd, PSTR("R1KP=")), 0.0f, 65.535f, &value))
        {
            slice.relayHeater1.Kp = value;
            Serial.print(F("R1 Kp: "));
            Serial.println(value);
        }
        else
        {
            Serial.println(F("Kp must be 0 to 65.535"));
        }
    }
    else if (starts_with_P(cmd, PSTR("R1KI=")))
    {
        float value;
        if (bsc::parseFloat(after_prefix_P(cmd, PSTR("R1KI=")), 0.0f, 65.535f, &value))
        {
            slice.relayHeater1.Ki = value;
            Serial.print(F("R1 Ki: "));
            Serial.println(value);
        }
        else
        {
            Serial.println(F("Ki must be 0 to 65.535"));
        }
    }
    else if (starts_with_P(cmd, PSTR("R1KD=")))
    {
        float value;
        if (bsc::parseFloat(after_prefix_P(cmd, PSTR("R1KD=")), 0.0f, 65.535f, &value))
        {
            slice.relayHeater1.Kd = value;
            Serial.print(F("R1 Kd: "));
            Serial.println(value);
        }
        else
        {
            Serial.println(F("Kd must be 0 to 65.535"));
        }
    }
    else if (starts_with_P(cmd, PSTR("R2KP=")))
    {
        float value;
        if (bsc::parseFloat(after_prefix_P(cmd, PSTR("R2KP=")), 0.0f, 65.535f, &value))
        {
            slice.relayHeater2.Kp = value;
            Serial.print(F("R2 Kp: "));
            Serial.println(value);
        }
        else
        {
            Serial.println(F("Kp must be 0 to 65.535"));
        }
    }
    else if (starts_with_P(cmd, PSTR("R2KI=")))
    {
        float value;
        if (bsc::parseFloat(after_prefix_P(cmd, PSTR("R2KI=")), 0.0f, 65.535f, &value))
        {
            slice.relayHeater2.Ki = value;
            Serial.print(F("R2 Ki: "));
            Serial.println(value);
        }
        else
        {
            Serial.println(F("Ki must be 0 to 65.535"));
        }
    }
    else if (starts_with_P(cmd, PSTR("R2KD=")))
    {
        float value;
        if (bsc::parseFloat(after_prefix_P(cmd, PSTR("R2KD=")), 0.0f, 65.535f, &value))
        {
            slice.relayHeater2.Kd = value;
            Serial.print(F("R2 Kd: "));
            Serial.println(value);
        }
        else
        {
            Serial.println(F("Kd must be 0 to 65.535"));
        }
    }
    else if (starts_with_P(cmd, PSTR("R1PERIOD=")))
    {
        long value;
        if (bsc::parseLong(after_prefix_P(cmd, PSTR("R1PERIOD=")), RLHT_PERIOD_MIN_MS, RLHT_PERIOD_MAX_MS, &value))
        {
            setRelayPeriod(1, (uint16_t)value);
            Serial.print(F("R1 period: "));
            Serial.print((int)value);
            Serial.println(F("ms"));
        }
        else
        {
            Serial.println(F("Period range 100-10000ms"));
        }
    }
    else if (starts_with_P(cmd, PSTR("R2PERIOD=")))
    {
        long value;
        if (bsc::parseLong(after_prefix_P(cmd, PSTR("R2PERIOD=")), RLHT_PERIOD_MIN_MS, RLHT_PERIOD_MAX_MS, &value))
        {
            setRelayPeriod(2, (uint16_t)value);
            Serial.print(F("R2 period: "));
            Serial.print((int)value);
            Serial.println(F("ms"));
        }
        else
        {
            Serial.println(F("Period range 100-10000ms"));
        }
    }
    else if (starts_with_P(cmd, PSTR("WDOG=")))
    {
        long v;
        if (!bsc::parseLong(after_prefix_P(cmd, PSTR("WDOG=")), 0, 65535, &v))
        {
            Serial.println(F("Ignored: expected a whole number from 0 to 65535"));
            return;
        }
        // Re-arms without clearing a trip, as SET_WATCHDOG does.
        commandWatchdog.arm((uint16_t)v, millis());
        Serial.print(F("WDOG-> "));
        if (v == 0)
            Serial.println(F("disarmed"));
        else
        {
            Serial.print(v);
            Serial.println(F(" ms"));
        }
    }
    else if (strcmp_P(cmd, PSTR("WDCLEAR")) == 0)
    {
        // The local operator's clear, as CLEAR_WATCHDOG_TRIP: the trip and
        // nothing else. Mode, setpoint and on-time commands were ignored
        // during the trip, so nothing heats until new ones are sent.
        commandWatchdog.clear();
        Serial.println(F("WDOG trip cleared"));
    }
    else if (starts_with_P(cmd, PSTR("HELP")) || starts_with_P(cmd, PSTR("?")))
    {
        Serial.println(F("Commands:"));
        Serial.println(F("MODE=CLOSED_LOOP/OPEN_LOOP"));
        Serial.println(F("R1TEMP=<val> - R1 setpoint (CLOSED)"));
        Serial.println(F("R2TEMP=<val> - R2 setpoint (CLOSED)"));
        Serial.println(F("R1TIME=<val> - R1 time ms (OPEN)"));
        Serial.println(F("R2TIME=<val> - R2 time ms (OPEN)"));
        Serial.println(F("R1TC=1/2 - R1 thermocouple"));
        Serial.println(F("R2TC=1/2 - R2 thermocouple"));
        Serial.println(F("R1KP/KI/KD=<val> - R1 PID"));
        Serial.println(F("R2KP/KI/KD=<val> - R2 PID"));
        Serial.println(F("R1PERIOD=<val> - R1 period ms"));
        Serial.println(F("R2PERIOD=<val> - R2 period ms"));
        Serial.println(F("WDOG=<ms> - command watchdog (0=off)"));
        Serial.println(F("WDCLEAR - clear a watchdog trip"));
    }
    else
    {
        Serial.println(F("Invalid cmd! Type HELP"));
    }
}

void serialCommands()
{
    while (Serial.available())
    {
        char c = (char)Serial.read();
        if (c == '\n')
        {
            cmdBuf[cmdIdx] = '\0';
            processCommand(cmdBuf);
            cmdIdx = 0;
            // One line per loop() pass: each reply can block on a full
            // transmit buffer, so the rest waits and the e-stop is sampled
            // between lines (#39).
            return;
        }
        else if (cmdIdx < sizeof(cmdBuf) - 1)
        {
            cmdBuf[cmdIdx++] = c;
        }
        // else: silently discard overflow characters until newline.
    }
}
