// Serial numbers are parsed strictly (#51): the whole token must parse, the
// value must be finite and in range for its field, and anything else is
// refused with no change. Each row of #51 is a case here.
#include "host.h"
#include "globals.h"
#include <cstdio>
#include <string>

void setup();
void loop();

static void serial(const char *line)
{
    Serial.out.clear();
    Serial.in += line;
    Serial.in += "\n";
    loop();
}

// The command printed its success reply.
static bool applied(const char *reply) { return Serial.out.find(reply) != std::string::npos; }

static void refuses(const char *line, const char *reply, bool unchanged)
{
    serial(line);
    char what[96];
    snprintf(what, sizeof what, "%s is refused with no change", line);
    host_check(what, !applied(reply) && !Serial.out.empty() && unchanged);
}

int main()
{
    setup();
    loop();

    // Gains: finite, 0 to 65.535, the range a u16 x1000 SET_PID would carry
    // (bread-crumbs-contracts#64). A relay heater's Kp is in ms of on-time
    // per degree, so tens are ordinary.
    serial("R1KP=1.5");
    host_check("R1KP=1.5 is applied", applied("R1 Kp: ") && slice.relayHeater1.Kp == 1.5);
    for (const char *line : {"R1KP=abc", "R1KP=", "R1KP=inf", "R1KP=1e9", "R1KP=2.5xyz", "R1KP=-1", "R1KP=70"})
        refuses(line, "R1 Kp: ", slice.relayHeater1.Kp == 1.5);
    serial("R1KP=50");
    host_check("R1KP=50 is applied", applied("R1 Kp: ") && slice.relayHeater1.Kp == 50);
    serial("R1KD=0.5");
    refuses("R1KD=inf", "R1 Kd: ", slice.relayHeater1.Kd == 0.5);

    // Setpoint: 0 to 500 C, closed loop.
    serial("R1TEMP=100");
    host_check("R1TEMP=100 is applied", applied("Relay 1 setpoint: ") && slice.relayHeater1.setpointTemperature == 100);
    for (const char *line : {"R1TEMP=abc", "R1TEMP=", "R1TEMP=499junk", "R1TEMP=nan"})
        refuses(line, "Relay 1 setpoint: ", slice.relayHeater1.setpointTemperature == 100);
    // The ceiling is RLHT_SETPOINT_MAX_C, shared with SET_SETPOINTS (#47).
    serial("R1TEMP=500");
    host_check("R1TEMP=500 is applied", applied("Relay 1 setpoint: ") && slice.relayHeater1.setpointTemperature == 500);
    refuses("R1TEMP=500.1", "Relay 1 setpoint: ", slice.relayHeater1.setpointTemperature == 500);
    host_check("R1TEMP=500.1 replies out of range", applied("Temp out of range (0-500C)"));

    // Thermocouple select: 1 or 2. Heater 1 off first: a heater heating in
    // closed loop keeps its select (#54).
    serial("R1TEMP=0");
    serial("R1TC=2");
    host_check("R1TC=2 is applied", applied("R1 TC: ") && slice.relayHeater1.thermocoupleSelect == 2);
    for (const char *line : {"R1TC=1junk", "R1TC=2.9", "R1TC="})
        refuses(line, "R1 TC: ", slice.relayHeater1.thermocoupleSelect == 2);

    // Period: RLHT_PERIOD_MIN_MS to RLHT_PERIOD_MAX_MS, applied on the next pass.
    serial("R1PERIOD=2000");
    loop();
    host_check("R1PERIOD=2000 is applied", slice.relayHeater1.relayPeriod == 2000);
    for (const char *line : {"R1PERIOD=100junk", "R1PERIOD=abc", "R1PERIOD=50"})
    {
        refuses(line, "R1 period: ", true);
        loop();
        host_check("the period is unchanged", slice.relayHeater1.relayPeriod == 2000);
    }

    // On-time: 0 to the period, open loop.
    serial("MODE=OPEN_LOOP");
    serial("R1TIME=200");
    host_check("R1TIME=200 is applied", applied("R1 time: ") && slice.relayHeater1.relayOnTime == 200);
    for (const char *line : {"R1TIME=abc", "R1TIME=", "R1TIME=200ms"})
        refuses(line, "R1 time: ", slice.relayHeater1.relayOnTime == 200);

    // The watchdog: a typo no longer disarms it.
    serial("WDOG=500");
    host_check("WDOG=500 arms the watchdog", applied("WDOG-> ") && commandWatchdog.timeoutMs() == 500);
    for (const char *line : {"WDOG=abc", "WDOG=", "WDOG=-1", "WDOG=65536"})
        refuses(line, "WDOG-> ", commandWatchdog.timeoutMs() == 500);
    serial("WDOG=0");
    host_check("WDOG=0 still disarms it", applied("disarmed") && commandWatchdog.timeoutMs() == 0);

    return host_finish();
}
