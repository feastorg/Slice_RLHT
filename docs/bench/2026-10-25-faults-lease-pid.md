---
title: "2026-10-25: setpoint limits, heater faults, the lease and wide gains"
parent: Bench Records
---

# 2026-10-25: setpoint limits, heater faults, the lease and wide gains

**Status: plan.** Expected values come from the host tests and the PRs;
Observed and Result are filled in at the bench.

**Verifies:** on `main` at `236a0c2`,
[#64](https://github.com/feastorg/Slice_RLHT/pull/64) (setpoint limits, #47),
[#65](https://github.com/feastorg/Slice_RLHT/pull/65) (heater faults, #48),
[#66](https://github.com/feastorg/Slice_RLHT/pull/66) (the commands-only
watchdog lease) and [#67](https://github.com/feastorg/Slice_RLHT/pull/67)
(12-byte SET_PID), with bread-crumbs-contracts 0.11.0
([#63](https://github.com/feastorg/Slice_RLHT/pull/63)).
**By:** @CameronBrooks11

## Setup

| | |
| --- | --- |
| Firmware | `236a0c2`, env `gen2_nano`. bread-slice-core 0.1.0, CRUMBS 0.16.2, bread-crumbs-contracts 0.11.0. Optional F4: the same commit built with `-DRLHT_OVERTEMP_C=30 -DRLHT_SETPOINT_MAX_C=25` |
| Board | gen2 Nano (ATmega328P) |
| Controller | Raspberry Pi 4 Model B, I2C at 50 kHz, address 19, CRUMBS Python binding 0.16.2 and contracts 0.11.0, via [`rlht_bench.py`](assets/2026-10-25-faults-lease-pid/rlht_bench.py) (this record's copy adds `wdl`, `pid`, `pid6`, `faults`, `fclear`, `raw` and `watch`). USB serial for P2 |
| Power | backplane (I2C and 12 V), one RLHT on the bus |
| Outputs | an LED on each relay output; two MAX6675 thermocouples at room temperature, not on a heater. E-stop: a jumper from D2 to GND |

## Steps and Results

| Step | Expected | Observed | Result |
| --- | --- | --- | --- |
| B1 baseline: `version`, `caps`, `state`, `wdog`, `faults` | module 1.0.0; caps level 1, flags `0x7FFF` (`0x7FF` plus `RLHT_CAP_WATCHDOG_LEASE`, `_PID_X1000`, `_FAULTS`, `_SETPOINT_LIMITS`); mode 0, estop 0, fault 0, relays 0,0, both `t` near room temperature; watchdog disarmed; faults `0x00` | | |
| L1 setpoint limits (#47): `mode 1`, `sp 40 0`; `mode 0`, `sp 500.1 0`; `sp 40 0`, `state`; `sp 0 0` | in open loop `sp` acked status 2 reason 6 (WRONG_MODE); above 500 C reason 4 (UNDEFINED_VALUE), `sp` still 0; then 40 C applied, `sp=40.0`, relay 1 LED on (room temperature is below 40) | | |
| F1 sensor fault (#48): `mode 0`, `sp 40 0`; unplug thermocouple 1; `watch 3`; `faults`; `sp 40 0`; `raw 0x7B 00`; plug it back in; `fclear`, `state`, `faults` | within about 1 s of unplugging (3 readings): fault 1, relays 0,0, `sp=0.0`, LED off; faults `0x04` H1_SENSOR; `sp` acked status 2 reason 8 (FAULT); `raw 0x7B 00` (CLEAR_FAULTS with a payload) reason 1, still latched; `fclear` applied, fault 0, `sp` still 0, nothing on | | |
| F2 no rise (#48): `pid 10 0 0`, `mode 0`, `sp 400 0`, `watch 130`; `fclear`; `pid6 1 0 0` | relay 1 on continuously (`on1` = period); about 120 s later fault 1, relays off; faults `0x02` H1_NO_RISE. The thermocouple is on no heater, so its temperature does not rise | | |
| F3 an idle heater can run with no probe: unplug thermocouple 2 with `sp` 0 on heater 2; `mode 1`, `duty 100 0`, `watch 3`; `duty 0 0`; plug it back in | no fault: heater 2 has no setpoint, and open loop does not check the sensor | | |
| F4 (optional) over-temperature, with the test build: flash it, `mode 1`, `duty 100 0`; warm thermocouple 1 in your fingers, `watch 30`; let it cool, `fclear` | once `t` passes 30 C, fault 1, relays off, faults `0x01` H1_OVERTEMP, in open loop. `fclear` while still above 30 latches again on the next reading | | |
| W1 the lease: `mode 1`, `duty 100 100`, `wdl 1000 1`, `watch 3`; `wd 0`, `clear`; `duty 100 100`, `wd 1000`, `watch 3`; `wd 0`, `duty 0 0`; `wdl 1000 2`, `wdog` | with the lease, GET_STATE polls alone let it trip about 1 s after `wdl`: relays off, LEDs off, GET_WATCHDOG tripped 1. Disarming before `clear` matters: under the lease `clear` refreshes nothing. With the 2-byte `wd 1000` the same polls keep it alive: no trip in 3 s. `wdl 1000 2` acked status 2 reason 4, and `wdog` reads disarmed | | |
| P1 wide gains: `pid 50 0.125 1.003`; `raw 0x03` with 13 bytes `50c300000000 50c300000000 00` (no spaces) | the 12-byte SET_PID acked status 1; the 13-byte one reason 1 (MALFORMED) | | |
| P2 serial: `prep`, `ser READ` | the READ line shows `PID1:50.00,0.13,1.00` (two decimals), and ends `FAULT:0x0` | | |

## Raw Output

The commands behind each step and their output.

## Not Checked

Filled in at the bench.

## Notes

Filled in at the bench.
