---
title: "2026-10-24: bread-slice-core, serial burst and strict parsing"
parent: Bench Records
---

# 2026-10-24: bread-slice-core, serial burst and strict parsing

**Status: complete.** Run 2026-10-25. Every step passed. Expected values
come from the host tests and the issues.

**Verifies:** on `main` at `03089e1`,
[#57](https://github.com/feastorg/Slice_RLHT/pull/57) (e-stop, watchdog and
status LED from bread-slice-core 0.1, FastLED gone),
[#58](https://github.com/feastorg/Slice_RLHT/pull/58) (one serial line per loop
pass, #39) and [#59](https://github.com/feastorg/Slice_RLHT/pull/59) (strict
serial numbers, #51) with [#61](https://github.com/feastorg/Slice_RLHT/pull/61)
(serial gains up to 65.535).
**By:** @CameronBrooks11

## Coverage

What each change can differ by, and the board that covers it:

| Change | Differs by | Covered on |
| --- | --- | --- |
| #57 status LED | MCU (the D5 port differs), gen (gen1 has none) | gen2 Nano. The 4809 port is covered by Slice_DCMT's gen2 Nano Every run of the same driver |
| #57 e-stop and watchdog | relay count (gen) | gen2 Nano (two relays), gen1 Nano Every (one) |
| #58 serial burst | nothing board-specific | gen2 Nano |
| #59 strict parsing | nothing board-specific; avr-libc is the same on both MCUs | gen2 Nano |

Not run, and why:

- gen2 Nano Every and gen1 Nano: every cell above is covered by the two
  boards that are run.
- A heater: no change touches the PID or the heater drive. The relay pin is
  what the holds change, and an LED on it shows that.
- The boot watchdog restamp: it only acts with `RLHT_WATCHDOG_BOOT_MS` set,
  which no build sets; host-tested in bread-slice-core.

## Setup

| | |
| --- | --- |
| Firmware | `03089e1`, envs `gen2_nano` and `gen1_nanoevery`. bread-slice-core 0.1.0, CRUMBS 0.16.2, bread-crumbs-contracts 0.10.0 |
| Boards | gen2 Nano (ATmega328P); gen1 Nano Every (ATmega4809) |
| Controller | Raspberry Pi 4 Model B, I2C at 50 kHz, address 19, CRUMBS Python binding 0.16.2 and contracts 0.10.0, via [`rlht_bench.py`](assets/2026-10-24-core-and-serial/rlht_bench.py). USB serial only for R5 and R6 |
| Power | backplane (I2C and 12 V), one RLHT on the bus at a time |
| Outputs | an LED (or the relay's own indicator) on each relay output; two MAX6675 thermocouples (one if only one is fitted: channel 2 then reads open). E-stop: a jumper from D2 to GND (D2 has a pull-up; LOW is e-stop) |

## Order

1. Flash the gen2 Nano from the workstation, put it on the backplane, USB to
   the Pi. Run R1 to R6.
2. Flash the gen1 Nano Every, put it on the backplane in place of the gen2
   Nano. Run R1 to R4. No serial needed.

Flashing, and every step that switches a relay, waits for your go.

## Steps and Results: gen2 Nano

| Step | Expected | Observed | Result |
| --- | --- | --- | --- |
| R1 boot LED: power-cycle, D2 open; then with D2 to GND | blue ~1 s then green; blue then red | blue then green; blue then red | pass |
| R2 baseline: `version`, `caps`, `state`, `wdog` | caps level 1, flags `0x7FF`; mode 0 (closed loop), estop 0, relays 0,0, `t` near room temperature on both channels, periods 1000, tc 1,2; watchdog disarmed | CRUMBS 1602, module 1.0.0; caps level 1, flags `0x7FF`; mode 0, estop 0, relays 0,0, `t=23.0,23.0`, sp 0, periods 1000, tc 1,2, `estop_count=0`; watchdog disarmed. A boot with D2 held, then released, read `estop_count=1` | pass |
| R3 e-stop hold: `mode 1`, `duty 100 100`, `state`; D2 to GND, `state`; `duty 100 100`; D2 open, `state` | both relay LEDs on, `on=1000,1000`; then estop 1, count 1, relays 0,0, `on=0,0`, both LEDs off; `duty` refused reason 3 (ESTOP); after release estop 0, LEDs stay off, `on=0,0` | `relays=1,1`, `on=1000,1000`, both LEDs on; on the press `estop=1`, count 0 -> 1, `relays=0,0`, `on=0,0`, LEDs off; `duty` acked status 2 reason 3; after release `estop=0`, `relays=0,0` over 1.3 s, LEDs off | pass |
| R4 watchdog trip: `duty 100 100`, `wd 500`, wait 1.2 s, `wdog`, `state`; `duty 100 100`; `wd 1000`, `wdog`; `clear`, `wdog`, `state` | tripped 1, trips 1, relays off, `on=0,0`; `duty` refused reason 2 (WATCHDOG_TRIP); still tripped after `wd 1000`; `clear` applied, tripped 0, trips 1, relays stay off. Then `wd 0`, so the board doesn't trip again while idle | `wdog` `01 f401 01 01` after the wait; `relays=0,0`, `on=0,0`; `duty` acked status 2 reason 2; after `wd 1000` `01 e803 01 01`; `clear` applied, `01 e803 00 01`, relays off; after `wd 0` `00 0000 00 01` | pass |
| R5 serial burst (#39): `prep`, then `flood 15`; ~5 s in, D2 to GND for 2 s, then open | `estop=1` within one or two 20 ms polls of the press, count +1; no failed polls; no reboot (a reboot shows as a boot blue and the count dropping back) | 30 s run: `estop=1`, count 1 -> 2 at 17:07:26.169, `estop=0` at 17:07:30.930; 6344 lines, 793 polls, 0 failed; the count never dropped back | pass |
| R6 strict parsing (#51): `mode 0` (`R1TEMP=` is checked only in closed loop). Each malformed line follows a valid one, so the old parser's result would differ: `ser "R1TEMP=40" "R1TEMP=499junk" "R1TC=1" "R1TC=2.9" "WDOG=60000" "WDOG=abc" "R1KP=1.5" "R1KP=inf" "R1KP=50"`, then `state`, `wdog`; then `ser "R1TEMP=0" "WDOG=0"` | the valid lines reply `Relay 1 setpoint: 40.00`, `R1 TC: 1`, `WDOG-> 60000 ms`, `R1 Kp: 1.50`, `R1 Kp: 50.00` (#59 alone refused it, with its 25.5 cap); each malformed one is refused with its range message (`Temp out of range (0-500C)`, `TC select 1 or 2`, `Ignored: expected a whole number from 0 to 65535`, `Kp must be 0 to 65.535`). `state` shows `sp=40.0`, `tc=1`; `wdog` armed at 60000. Before #59 these read 499, 2 and disarmed. Relay 1 may switch on while the setpoint is 40 (an LED load). Kp isn't in GET_STATE; that `R1KP=inf` leaves it at 1.5 is host-tested | as expected, line for line: the five valid lines applied (`R1 Kp: 50.00` among them), the four malformed ones refused with those messages. `state` `sp=40.0`, `tc=1`, relay 1 on (`on=800`); `wdog` `01 60ea 00 01`. Reset to `R1TEMP=0`, `WDOG=0` afterwards | pass |

## Steps and Results: gen1 Nano Every

| Step | Expected | Observed | Result |
| --- | --- | --- | --- |
| R1 boot: power-cycle | no LED activity (none fitted) | no LED activity. The board did not answer on I2C after the first power-up; after a later power-cycle on the backplane alone it did (see Notes) | pass |
| R2 baseline | as on the gen2 Nano, one relay | CRUMBS 1602, module 1.0.0; caps level 1, flags `0x7FF`; mode 0, estop 0, relays 0,0, `t=23.5,-3276.8` (channel 2 has no thermocouple), `estop_count=0`; watchdog disarmed | pass |
| R3 e-stop hold, with `duty 100 0` | as on the gen2 Nano, relay 1 only | `relays=1,0`, `on=1000,0`, LED on; on the press `estop=1`, count 0 -> 1, relays off, LED off; `duty` acked status 2 reason 3; after release `estop=0`, relays off over 1.3 s | pass |
| R4 watchdog trip, with `duty 100 0` | as on the gen2 Nano, relay 1 only | as on the gen2 Nano: `01 f401 01 01`, relays off; `duty` reason 2; `01 e803 01 01`; `clear` `01 e803 00 01`; `wd 0` `00 0000 00 01` | pass |

## Raw Output

The commands behind each step and their output:
[`raw-session.txt`](assets/2026-10-24-core-and-serial/raw-session.txt).

## Not Checked

- How fast the e-stop flag follows a press during R5: the jumper is placed by
  hand, so the 20 ms poll can't be timed against it.
- `R1KP=inf` leaving Kp at 1.5: Kp isn't in GET_STATE; host-tested.
- Channel 2 on the gen1 Nano Every: no thermocouple fitted.

## Notes

- **Temperatures read the sentinel just after a reset.** Two seconds after
  `prep`'s port open reset the gen2 Nano, GET_STATE read `t=-3276.8` on both
  channels; seven seconds later both read 23 to 24 °C.
- **The gen1 Nano Every did not answer at first.** After flashing and the
  first power-up on the backplane, every I2C write failed with `EIO` and a
  scan of 0x08 to 0x77 found nothing. With USB from the board to the
  controller plugged in, address 19 answered; it still answered with USB
  unplugged, and again after a power-cycle on the backplane alone. Not
  reproduced, so the cause is unknown; the gen2 Nano answered on the same
  backplane without USB.
- **R5's press landed early.** It came 2.2 s into the run and was held about
  4.8 s, not the planned ~5 s in and 2 s.

