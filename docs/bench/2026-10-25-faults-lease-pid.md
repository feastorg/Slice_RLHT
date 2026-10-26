---
title: "2026-10-25: setpoint limits, heater faults, the lease and wide gains"
parent: Bench Records
---

# 2026-10-25: setpoint limits, heater faults, the lease and wide gains

**Status: complete.** B1, L1, F2, F4, W1 and P1 passed; F1 passed except
the open-thermocouple path, which was not reached; F3 skipped; P2 not checked.
Expected values come from the host tests and the PRs.

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
| B1 baseline: `version`, `caps`, `state`, `wdog`, `faults` | module 1.0.0; caps level 1, flags `0x7FFF` (`0x7FF` plus `RLHT_CAP_WATCHDOG_LEASE`, `_PID_X1000`, `_FAULTS`, `_SETPOINT_LIMITS`); mode 0, estop 0, fault 0, relays 0,0, both `t` near room temperature; watchdog disarmed; faults `0x00` | CRUMBS 1602, module 1.0.0; caps `0101ff7f0000`: level 1, flags `0x7FFF`; mode 0, estop 0, fault 0, relays 0,0, `t=22.3,21.8`; watchdog disarmed; faults `0x00` | pass |
| L1 setpoint limits (#47): `mode 1`, `sp 40 0`; `mode 0`, `sp 500.1 0`; `sp 40 0`, `state`; `sp 0 0` | in open loop `sp` acked status 2 reason 6 (WRONG_MODE); above 500 C reason 4 (UNDEFINED_VALUE), `sp` still 0; then 40 C applied, `sp=40.0`, relay 1 LED on (room temperature is below 40) | open loop: status 2 reason 6; 500.1 C: status 2 reason 4, `sp=0.0`; 40 C: applied, `sp=40.0`. Relay 1 barely switched (`on=17` ms per 1000 ms): the default Kp 1 against a 17 C error, not a limit | pass |
| F1 sensor fault (#48): `mode 0`, `sp 40 0`; unplug thermocouple 1; `watch 3`; `faults`; `sp 40 0`; `raw 0x7B 00`; plug it back in; `fclear`, `state`, `faults` | within about 1 s of unplugging (3 readings): fault 1, relays 0,0, `sp=0.0`, LED off; faults `0x04` H1_SENSOR; `sp` acked status 2 reason 8 (FAULT); `raw 0x7B 00` (CLEAR_FAULTS with a payload) reason 1, still latched; `fclear` applied, fault 0, `sp` still 0, nothing on | the open-thermocouple fault was not reached. Unplugging thermocouple 1 produced one reading of 547.8 C, which latched H1_OVERTEMP (faults `0x01`) with relays off and `sp` zeroed; once out, it read open (NaN) steadily. While latched: SET_SETPOINTS, SET_MODE and SET_OPEN_DUTY acked status 2 reason 8; `raw 0x7B 00` reason 1, still latched. Plugged back in (t1 23.0), `fclear` applied: fault 0, faults `0x00`, `sp` 0, nothing on. A retry found the fault already latched as H1_OVERTEMP with nobody touching the rig (see Notes) | over-temperature, hold, refusals and clear: pass; sensor path: not reached |
| F2 no rise (#48): `pid 10 0 0`, `mode 0`, `sp 400 0`, `watch 130`; `fclear`; `pid6 1 0 0` | relay 1 on continuously (`on1` = period); about 120 s later fault 1, relays off; faults `0x02` H1_NO_RISE. The thermocouple is on no heater, so its temperature does not rise | the 12-byte `pid 10 0 0` applied; relay 1 on continuously (`on=1000`) from 22:40:48.7, t1 23.5 to 24.0 C; H1_NO_RISE latched at 22:42:48.7, 120.3 s after `sp 400` was applied (22:40:48.4); relays off, `sp` zeroed | pass |
| F3 an idle heater can run with no probe: unplug thermocouple 2 with `sp` 0 on heater 2; `mode 1`, `duty 100 0`, `watch 3`; `duty 0 0`; plug it back in | no fault: heater 2 has no setpoint, and open loop does not check the sensor | relay 1 on for 20 s in open loop, no fault; thermocouple 2 was never unplugged (t2 24.0 at the end), so the idle-heater case was not exercised | skipped |
| F4 (optional) over-temperature, with the test build: flash it, `mode 1`, `duty 100 0`; warm thermocouple 1 in your fingers, `watch 30`; let it cool, `fclear` | once `t` passes 30 C, fault 1, relays off, faults `0x01` H1_OVERTEMP, in open loop. `fclear` while still above 30 latches again on the next reading | test build confirmed (`sp 26` refused reason 4). Relay 1 on; t1 read 30.0 C at 22:57:06.2 with no latch, 30.3 C at 22:57:07.2 with H1_OVERTEMP latched and relay 1 off at that read; latched through 34.5 C. `fclear` at 34.8 C applied and re-latched on the next read 0.3 s later | pass |
| W1 the lease: `mode 1`, `duty 100 100`, `wdl 1000 1`, `watch 3`; `wd 0`, `clear`; `duty 100 100`, `wd 1000`, `watch 3`; `wd 0`, `duty 0 0`; `wdl 1000 2`, `wdog` | with the lease, GET_STATE polls alone let it trip about 1 s after `wdl`: relays off, LEDs off, GET_WATCHDOG tripped 1. Disarming before `clear` matters: under the lease `clear` refreshes nothing. With the 2-byte `wd 1000` the same polls keep it alive: no trip in 3 s. `wdl 1000 2` acked status 2 reason 4, and `wdog` reads disarmed | `wdl 1000 1` applied at 22:44:43.866; the poll at 22:44:45.074 read relays 0,0, on 0,0; GET_WATCHDOG `01 e803 01 01`. `wd 0` then `clear`: `00 0000 00 01`. With `wd 1000`, relays on for the 3 s of polls, `01 e803 00 01`. `wdl 1000 2` status 2 reason 4, `wdog` `00 0000 00 01` | pass |
| P1 wide gains: `pid 50 0.125 1.003`; `raw 0x03` with 13 bytes `50c300000000 50c300000000 00` (no spaces) | the 12-byte SET_PID acked status 1; the 13-byte one reason 1 (MALFORMED) | after a `prep` reset: the 12-byte `pid 50 0.125 1.003` acked status 1; the refusal frame went out as 14 bytes, not 13 (typo), and was acked status 2 reason 1, the same rule | pass |
| P2 serial: `prep`, `ser READ` | the READ line shows `PID1:50.00,0.13,1.00` (two decimals), and ends `FAULT:0x0` | `READ` replied `Invalid cmd! Type HELP`: RLHT has no READ command. The state line with `FAULT:` is printed only in `SLICE_DEBUG` builds | not checked |

## Raw Output

The commands behind each step and their output:
[`raw-session.txt`](assets/2026-10-25-faults-lease-pid/raw-session.txt).
Two long state streams are summarised there in one line each.

## Not Checked

- The open-thermocouple fault (H1_SENSOR) on hardware: both attempts
  latched over-temperature first. Host-tested in `test_heater_faults`.
- F3, an idle heater with its probe unplugged: the probe could not be
  removed without disturbing the rig. Host-tested.
- P2, the serial `FAULT:` field: only in `SLICE_DEBUG` builds.
- Heater 2's faults, and the gen1 Nano Every: not run.
- How soon the lease trips: bracketed between 0.3 s and 1.2 s after `wdl`
  by the 200 ms polls, which print only on a change.

## Notes

- **Over-temperature latched twice with no heater and the probes at room
  temperature.** In F1, one reading of 547.8 C came while thermocouple 1 was
  being unplugged. Later, with nobody touching the rig, H1_OVERTEMP was
  latched sometime between 22:46:10 and 22:49:34; that reading was not
  captured. A 37 s watch afterwards (60 reads) saw nothing above 29.9 C.
  Each check acts on a single reading, so one bad MAX6675 value above
  `RLHT_OVERTEMP_C` latches the fault. Safe, but a nuisance trip; the cause
  is not known.
- **The MAX6675 reads open once the probe is out.** After the unplug,
  thermocouple 1 read NaN steadily, so the open-thermocouple path would
  have latched H1_SENSOR had over-temperature not latched first.
- **#65's changelog names a `READ` line RLHT does not have** (P2).
- **The board is left on the F4 test image** (over-temperature at 30 C,
  setpoints up to 25 C). Reflash `main` before using it.
- **B1's first queries got EIO** during the boot wait.
- An arm resting on thermocouple 2 raised t2 to 30.3 C during F1.
