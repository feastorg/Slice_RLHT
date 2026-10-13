# Slice RLHT firmware

PlatformIO project for the RLHT relay heater controller. Build one
environment with `pio run -e <env>`; the environments are listed in
`platformio.ini` (`gen1_nano`, `gen2_nano`, `gen1_nanoevery`,
`gen2_nanoevery`).

## Serial commands

At 115200 baud, one command per line. `HELP` (or `?`) lists them.

| Command | Effect |
| --- | --- |
| `MODE=CLOSED_LOOP` / `MODE=OPEN_LOOP` | Control mode (also `0` / `1`) |
| `R1TEMP=<C>`, `R2TEMP=<C>` | Setpoint, closed loop; 0 is off |
| `R1TIME=<ms>`, `R2TIME=<ms>` | On-time per period, open loop |
| `R1TC=1/2`, `R2TC=1/2` | Thermocouple feeding each heater |
| `R1KP=`, `R1KI=`, `R1KD=` (and `R2…`) | PID gains |
| `R1PERIOD=<ms>`, `R2PERIOD=<ms>` | Relay period, 100-10000 ms |
| `WDOG=<ms>` | Arm the command watchdog, or disarm it with 0 |
| `WDCLEAR` | Clear a latched watchdog trip |

## Command watchdog

The watchdog boots disarmed unless the build defines
`RLHT_WATCHDOG_BOOT_MS`. Once armed, by `BREAD_OP_SET_WATCHDOG` or
`WDOG=<ms>`, it trips when no I2C command, I2C reply or serial line has
arrived within the timeout. A trip turns both relays off and zeroes both
setpoints and open-loop on-times.

The trip latches. Only these clear it:

- `BREAD_OP_CLEAR_WATCHDOG_TRIP` (0x7C) over I2C, with an empty
  payload (a frame that carries a payload is ignored and the trip stays);
- `WDCLEAR` over serial;
- a reboot.

Re-arming or disarming (`BREAD_OP_SET_WATCHDOG`, `WDOG=<ms>`, with 0
to disarm) and ordinary traffic keep the watchdog fed but do not clear
a trip, so a logger or a poll cannot release the hold. GET_CAPS
advertises `RLHT_CAP_CLEAR_WATCHDOG_TRIP` to say so.

While the trip is set, mode, setpoint and open-loop duty commands are
ignored: `SET_MODE`, `SET_SETPOINTS` and `SET_OPEN_DUTY` over I2C, and
`MODE=`, `R1TEMP=`, `R2TEMP=`, `R1TIME=` and `R2TIME=` over serial
(serial prints `Ignored: watchdog tripped, send WDCLEAR first`). PID
gains, periods, thermocouple selection and the watchdog timeout are
still accepted, since none of them can start a heater.

Clearing a trip resumes nothing. The mode is the one in force when the
trip fired, the setpoints and on-times are 0, and nothing heats until a
controller or an operator sends a new setpoint or duty after the clear.
A closed-loop heater given a setpoint then starts its PID from an
on-time of 0.
