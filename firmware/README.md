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
| `R1TEMP=<C>`, `R2TEMP=<C>` | Setpoint, closed loop, 0 to `RLHT_SETPOINT_MAX_C` (500); 0 is off |
| `R1TIME=<ms>`, `R2TIME=<ms>` | On-time per period, open loop |
| `R1TC=1/2`, `R2TC=1/2` | Thermocouple feeding each heater; ignored while that heater heats in closed loop |
| `R1KP=`, `R1KI=`, `R1KD=` (and `R2…`) | PID gains |
| `R1PERIOD=<ms>`, `R2PERIOD=<ms>` | Relay period, 100-10000 ms |
| `WDOG=<ms>` | Arm the command watchdog, or disarm it with 0 |
| `WDCLEAR` | Clear a latched watchdog trip |
| `FAULTCLEAR` | Clear latched heater faults |

## Command watchdog

The watchdog boots disarmed unless the build defines
`RLHT_WATCHDOG_BOOT_MS`. Once armed, by `BREAD_OP_SET_WATCHDOG` or
`WDOG=<ms>`, it trips when no I2C command, I2C reply or serial line has
arrived within the timeout. A trip turns both relays off and zeroes both
setpoints and open-loop on-times.

A controller can arm it as a lease instead (`RLHT_CAP_WATCHDOG_LEASE`): a
3-byte `SET_WATCHDOG`, `[timeout_ms:u16][flags:u8]`, with
`BREAD_WATCHDOG_FLAG_COMMANDS_ONLY`. Then only `SET_MODE`, `SET_SETPOINTS`,
`SET_OPEN_DUTY` and `SET_WATCHDOG` refresh it; replies, other commands and
serial lines do not, so a status poller that outlives the control path no
longer keeps the heaters on. The control path re-sends `SET_WATCHDOG` as its
keepalive. The 2-byte form and serial `WDOG=` end the lease. A flag this
firmware does not define is refused `BREAD_REFUSE_UNDEFINED_VALUE` and arms
nothing.

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
(serial prints `Ignored: watchdog tripped, send WDCLEAR first`). The I2C
ones are refused with `BREAD_REFUSE_WATCHDOG_TRIP`, so GET_ACK says so
(see Refused commands). PID gains, periods, thermocouple selection and
the watchdog timeout are still accepted, since none of them can start a
heater.

Clearing a trip resumes nothing. The mode is the one in force when the
trip fired, the setpoints and on-times are 0, and nothing heats until a
controller or an operator sends a new setpoint or duty after the clear.
A closed-loop heater given a setpoint then starts its PID from an
on-time of 0.

## Heater faults

A heater fault (#48) latches and holds both heaters off the way a watchdog
trip does: the relays go off, both setpoints and open-loop on-times go to 0,
and both PIDs drop to manual. The checks run on every thermocouple reading,
on the thermocouple each heater is set to (`R1TC=`/`R2TC=`):

| Fault | When | Bit |
| --- | --- | --- |
| Over-temperature | `RLHT_OVERTEMP_READS` (3) readings in a row above `RLHT_OVERTEMP_C` (525 C), in either mode | `RLHT_FAULT_H1_OVERTEMP` / `_H2_` |
| No rise | closed loop with a setpoint: the PID output has stayed at or above `RLHT_NO_RISE_OUTPUT_PCT` (90 %) of the period for `RLHT_NO_RISE_MS` (120 s), and the temperature has risen less than `RLHT_NO_RISE_DELTA_C` (2 C) | `RLHT_FAULT_H1_NO_RISE` / `_H2_` |
| Sensor | closed loop with a setpoint: `RLHT_SENSOR_FAULT_READS` (3) readings in a row of an open thermocouple | `RLHT_FAULT_H1_SENSOR` / `_H2_` |

The values in brackets are starting points, not measurements; each is a
build flag. The no-rise window has to be longer than the slowest healthy
heat-up from cold at full output. A heater with no setpoint, or in open
loop, can have its thermocouple unplugged without a fault. Gen1 boards check
heater 1 only.

`BREAD_OP_GET_FAULTS` reports the latched bits, and GET_STATE sets
`RLHT_FLAG_FAULT` while any is latched. While one is latched, `SET_MODE`,
`SET_SETPOINTS` and `SET_OPEN_DUTY` are refused with `BREAD_REFUSE_FAULT`,
and the serial `MODE=`, `R1TEMP=`/`R2TEMP=` and `R1TIME=`/`R2TIME=` are
ignored with a message. `BREAD_OP_CLEAR_FAULTS` (empty payload) or serial
`FAULTCLEAR` clears every fault and resumes nothing; a condition that
persists, such as an over-temperature, latches again on the next reading.
GET_CAPS advertises `RLHT_CAP_FAULTS`.

A firmware check supplements an independent thermal cutoff; it does not
replace one.

## Refused commands

A SET the firmware does not act on in full is refused with
`crumbs_refuse()`, so CRUMBS's GET_ACK reports it refused, with a reason
from bread-crumbs-contracts `bread_refuse.h`. GET_CAPS advertises
`RLHT_CAP_REFUSES`, `RLHT_CAP_ESTOP_HOLD`, `RLHT_CAP_SETPOINT_LIMITS`,
`RLHT_CAP_FAULTS`, `RLHT_CAP_WATCHDOG_LEASE` and `RLHT_CAP_PID_X1000`.

| Reason | When |
| --- | --- |
| `BREAD_REFUSE_MALFORMED` | the payload does not unpack; SET_PID is not 6 bytes (u8 gains x10) or 12 bytes (u16 gains x1000, `RLHT_CAP_PID_X1000`); SET_WATCHDOG lacks its u16; CLEAR_WATCHDOG_TRIP or CLEAR_FAULTS carries a payload |
| `BREAD_REFUSE_ESTOP` | SET_MODE, SET_SETPOINTS or SET_OPEN_DUTY while the hardware e-stop is held; this reason wins when a watchdog trip is held too |
| `BREAD_REFUSE_WATCHDOG_TRIP` | SET_MODE, SET_SETPOINTS or SET_OPEN_DUTY while a trip is held; this reason wins over a latched fault |
| `BREAD_REFUSE_FAULT` | SET_MODE, SET_SETPOINTS or SET_OPEN_DUTY while a heater fault is latched |
| `BREAD_REFUSE_UNDEFINED_VALUE` | SET_MODE other than open or closed loop; a SET_WATCHDOG flag other than `BREAD_WATCHDOG_FLAG_COMMANDS_ONLY`; SET_TC_SELECT with a channel other than 1 or 2; SET_PERIODS outside `RLHT_PERIOD_MIN_MS`-`RLHT_PERIOD_MAX_MS` (100-10000 ms); SET_OPEN_DUTY above `RLHT_OPEN_DUTY_MAX_PCT` (100 %); SET_SETPOINTS above `RLHT_SETPOINT_MAX_C` (500 C, a build flag) |
| `BREAD_REFUSE_WRONG_MODE` | SET_OPEN_DUTY outside open loop; SET_SETPOINTS outside closed loop; SET_TC_SELECT changing the thermocouple of a heater that is in closed loop with a setpoint above 0 |
| `BREAD_REFUSE_UNKNOWN_OP` | an opcode with no registered command handler, including a GET opcode sent as a command frame instead of through SET_REPLY |

A SET_TC_SELECT, SET_PERIODS or SET_OPEN_DUTY with one channel out of
range still sets the other channel, then refuses. A heater's thermocouple
changes with its setpoint at 0 or in open loop. Its PID then restarts from
0 on the new sensor, since the swap would otherwise read as a step in the
measurement (#54). A SET_SETPOINTS with
either setpoint above the ceiling stores neither. GET_STATE shows what took
effect. A hold is checked before the payload is unpacked, so a malformed
SET_MODE, SET_SETPOINTS or SET_OPEN_DUTY sent during a hold reads the
hold's reason. The serial `R1PERIOD=`/`R2PERIOD=` commands
still clamp a period to the same range. Every valid frame refreshes the
watchdog, a refused one included.

The firmware declares its type (`RLHT_TYPE_ID`) to CRUMBS, so a frame typed
for another family is rejected before any handler runs: GET_ACK reports it
rejected with `CRUMBS_ACK_REJECT_TYPE`, and it does not refresh the
watchdog. Frames of type `0x00` are accepted. GET_ACK replies carry type
`0x01`.

## Hardware e-stop

The e-stop input is D2 with the internal pull-up; a press pulls it LOW. The
firmware samples it on every loop pass and through the 1 s boot wait. The
first LOW sample asserts the e-stop, and release needs the line HIGH for
25 ms without a LOW sample in between, so a chattering line stays stopped.
A LOW glitch that a loop pass samples, such as noise on an unpressed line,
asserts the e-stop too.

A press holds the way a watchdog trip does (`RLHT_CAP_ESTOP_HOLD`): the
relays go off, both setpoints and open-loop on-times go to 0, and both PIDs
drop to manual. While the e-stop is held, `SET_MODE`, `SET_SETPOINTS` and
`SET_OPEN_DUTY` are refused with `BREAD_REFUSE_ESTOP`, and the serial
`MODE=`, `R1TEMP=`/`R2TEMP=` and `R1TIME=`/`R2TIME=` are ignored with a
message. PID gains, periods, thermocouple selection and the watchdog stay
available. `GET_STATE` shows the e-stop flag, and `estop_count` counts the
press.

Releasing the e-stop heats nothing: the controller resumes by sending
setpoints or duties.

The firmware hold is the only stop on gen1 boards, which have no hardware
cut of heater power on the e-stop line. Check a later board's schematic
before relying on a hardware cut. An e-stop that cuts the supply instead
reboots the slice with setpoints at 0; power the Nano from the switched
supply too, since a Nano kept alive over USB sees no stop.
bread-crumbs-contracts `docs/controller-compatibility.md` covers both
wirings.
