#include "globals.h"
#include "config.h"

void printSliceState(Print &out)
{
    out.print(F("ControlMode: "));
    out.print(slice.mode == CLOSED_LOOP ? F("CLOSED_LOOP") : F("OPEN_LOOP"));
    out.print(F(", T1:"));
    out.print(slice.temperature1);
    out.print(F(", T2:"));
    out.print(slice.temperature2);

    out.print(F(", PID1:"));
    out.print(slice.relayHeater1.Kp);
    out.print(F(","));
    out.print(slice.relayHeater1.Ki);
    out.print(F(","));
    out.print(slice.relayHeater1.Kd);

    out.print(F(", Relay1Input:"));
    out.print(slice.relayHeater1.inputTemperature);
    out.print(F(", Setpoint1:"));
    out.print(slice.relayHeater1.setpointTemperature);
    out.print(F(", onTime1:"));
    out.print((int)slice.relayHeater1.relayOnTime);
    out.print(F(", rPeriod1:"));
    out.print(slice.relayHeater1.relayPeriod);

    out.print(F(", PID2:"));
    out.print(slice.relayHeater2.Kp);
    out.print(F(","));
    out.print(slice.relayHeater2.Ki);
    out.print(F(","));
    out.print(slice.relayHeater2.Kd);

    out.print(F(", Relay2Input:"));
    out.print(slice.relayHeater2.inputTemperature);
    out.print(F(", Setpoint2:"));
    out.print(slice.relayHeater2.setpointTemperature);
    out.print(F(", onTime2:"));
    out.print((int)slice.relayHeater2.relayOnTime);
    out.print(F(", rPeriod2:"));
    out.print(slice.relayHeater2.relayPeriod);
    out.print(F(", Thermo Select Relay 1:"));
    out.print(slice.relayHeater1.thermocoupleSelect);
    out.print(F(", Thermo Select Relay 2:"));
    out.print(slice.relayHeater2.thermocoupleSelect);
    out.print(F(", ESTOP:"));
    out.print(estopInput.asserted());

    out.print(F(", WDOG:"));
    uint16_t wdTimeout;
    bool wdTrip;
    uint8_t wdTrips;
    wdTimeout = commandWatchdog.timeoutMs();
    wdTrip = commandWatchdog.tripped();
    wdTrips = commandWatchdog.tripCount();
    if (wdTimeout == 0)
        out.print(F("off"));
    else
        out.print(wdTimeout);
    out.print(F(", WDTRIP:"));
    out.print(wdTrip);
    out.print(F("/"));
    out.print(wdTrips);
    out.print(F(", FAULT:0x"));
    out.println(heaterFaults, HEX);
}

void printSerialOutput()
{
    if (millis() - timing.lastSerialPrint >= SERIAL_UPDATE_TIME_MS)
    {
#ifdef SLICE_DEBUG
        printSliceState(Serial);
#endif

        timing.lastSerialPrint = millis();
    }
}
