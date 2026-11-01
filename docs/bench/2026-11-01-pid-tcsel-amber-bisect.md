---
title: "2026-11-01: PID timing, the tc-select lock, amber, and the #81 bisect"
parent: Bench Records
---

# 2026-11-01: PID timing, the tc-select lock, amber, and the #81 bisect

**Status: complete.** R1a, R2, R3 and F4 passed. R4 found what cuts
GET_STATE replies short (#81): removing the loop's work, removing only
`main.cpp`'s masked windows, or slowing the loop each took the rate from 187
bad replies in 300 s to 0.

**Verifies:** on `main` at `a251158`,
[#76](https://github.com/feastorg/Slice_RLHT/pull/76) (compute the heater
PIDs once per thermocouple reading),
[#77](https://github.com/feastorg/Slice_RLHT/pull/77) (refuse a thermocouple
select change under a running heater) and
[#83](https://github.com/feastorg/Slice_RLHT/pull/83) (amber on a watchdog
trip or heater fault), with CRUMBS 0.17.0, bread-crumbs-contracts 0.11.1 and
bread-slice-core 0.2.0.
**By:** @CameronBrooks11

## Setup

| | |
| --- | --- |
| Firmware | `a251158`, env `gen2_nano` (24038 B flash, 922 B RAM, `firmware.hex` sha256 `c926b0b35d60...`). CRUMBS 0.17.0, bread-crumbs-contracts 0.11.1, bread-slice-core 0.2.0. F4: the same commit built with `PLATFORMIO_BUILD_FLAGS="-DRLHT_OVERTEMP_C=30 -DRLHT_SETPOINT_MAX_C=25"`. R4: the same commit with [`i81-variants.patch`](assets/2026-11-01-pid-tcsel-amber-bisect/i81-variants.patch) applied and one flag per step (table below); then `main` reflashed |
| Board | gen2 Nano (ATmega328P) |
| Controller | Raspberry Pi 4 Model B, I2C at 50 kHz, address 19, CRUMBS Python binding 0.16.2 and contracts 0.11.0, via this session's [`rlht_bench.py`](assets/2026-11-01-pid-tcsel-amber-bisect/rlht_bench.py) (adds `tcsel` and `ontimes`) and the 2026-10-31 record's [`replylog.py`](assets/2026-10-31-refusal-latch-bus/replylog.py) and [`f4log.py`](assets/2026-10-31-refusal-latch-bus/f4log.py) |
| Power | backplane (I2C, 12 V and the e-stop line), with Slice_DCMT's gen2 Nano Every on the bus at address 21. The e-stop line is shared: the jumper counts on both boards |
| Outputs | an LED on each relay output; two MAX6675 thermocouples, not on a heater, warmed by hand in R2 and F4 |

## Steps and Results

| Step | Expected | Observed | Result |
| --- | --- | --- | --- |
| B0 baseline: `version`, `caps`, `state`, `wdog`, `faults` | CRUMBS 1700, module 1.0.0; caps `0101ff7f0000`; mode 0, relays off; watchdog disarmed; no faults | as expected; 21.5 C on both | pass |
| R1a mode 1, duty 50 50, `wdl 1000 1`, `watch 3`; jumper on, off; `wd 0`, `clear`, `duty 0 0` (#83) | relays blink, then off about 1 s in; LED amber; red with the jumper; amber after release; green after the clear | relays on at 11:09:20.04, off with `on=0,0` by 11:09:20.50; `wdog 01e8030101`; LED amber, red, amber; after `clear` `wdog 0000000001`, LED green, `duty 0 0` applied | pass |
| R2 `pid 10 0 25.5`, mode 0, `sp 40 0`, `ontimes 60`; TC1 warmed by hand from about t=20 s to t=40 s (#76) | on1 changes once per reading; derivative about 21 ms per 0.25 C step, not about 64 | steady 24.0 C: 160 ms; a step to 23.8: 183 ms (P alone 162, +21); a step to 24.3: 136 ms (P alone 157, -21). 600 polls, 0 failed, 177 on1 changes, median gap 0.30 s; 1 pair on consecutive polls, from an all-0xFF reply at 11:11:23.122 read as state (#81). TC1 26.0 C at 23.3 s, 37.0 C by 40 s | pass |
| R3 `tcsel` with heater 1 at a 40 C setpoint, then at setpoint 0, then in open loop (#77) | refused reason 6 while heating; applied otherwise | heating (`on=16`): `tcsel 2 2` status 2 reason 6, `tc=1,2`; `tcsel 1 2` applied; `sp 0 0`, `tcsel 2 2` applied, `tc=2,2`; mode 1, `tcsel 2 2` applied; back to `tc=1,2`, mode 0 | pass |
| F4 test build: `sp 26 0`, `sp 25 0`; mode 1, `duty 100 0`, `f4log.py 75` with TC1 warmed by hand; jumper on, off; cool below 30 C; `fclear` (#71, #83) | `sp 26` refused reason 4, `sp 25` applied; latch after three readings over 30.0 C: relay off, fault 1, LED amber; `duty 0 0` refused reason 8; red with the jumper, amber after; `fclear` clears, LED green | `sp 26` status 2 reason 4, `sp 25` applied. 30.3 C at 11:53:17.123, 30.8 C at 17.358; by 17.711 `relay1=0 fault=1 on1=0 flags=0x08`, held to the end at up to 33.8 C; `faults 0x01 H1_OVERTEMP`; `duty 0 0` reason 8; LED amber, red, amber (twice). Cleared at 27.3 C: `faults 0x00 none`, `fault=0` | pass |
| R4 #81 bisect: one flash and one 300 s `replylog.py 19 1 300` per build (below) | see below | see below | cause found |
| Final: `main` reflashed; `caps`, `faults`, `sp 26 0`; a closing 300 s capture | caps `0101ff7f0000`; `sp 26` applied (the 30 C test limits gone); the fault back at C0's rate | `firmware.hex` sha256 `c926b0b35d60...`; caps `0101ff7f0000`, `faults 0x00 none`, `sp 26` applied; 175 of 2552 bad | pass |

### R4: GET_STATE replies per 300 s

Every variant flag also forces both relays off. With no flag the patched tree
builds byte-identical to `main` (`firmware.hex` sha256 `c926b0b35d60...`).

| Build | Flag | What it removes | Flash / RAM (B) | Polls | Failed | All-0xFF passed CRC | Bad |
| --- | --- | --- | --- | --- | --- | --- | --- |
| C0 | none | nothing | 24038 / 922 | 2552 | 174 | 13 | 187 |
| V-IDLE | `-DI81_IDLE_LOOP` | all of `loop()` but the watchdog reset, e-stop poll and status LED; fixed temperatures | 13022 / 798 | 2553 | 0 | 0 | 0 |
| V-NOMASK | `-DI81_NO_LOOP_MASK` | every `noInterrupts()` window in `main.cpp`, nothing else | 23778 / 922 | 2553 | 0 | 0 | 0 |
| V-PAD | `-DI81_LOOP_PAD_US=300` | nothing: 300 us with interrupts on is added to each loop pass | 23836 / 922 | 2553 | 0 | 0 | 0 |
| main, closing | none | nothing | 24038 / 922 | 2552 | 171 | 4 | 175 |

## Raw Output

[`raw-session.txt`](assets/2026-11-01-pid-tcsel-amber-bisect/raw-session.txt):
every RLHT command and its output. The R4 captures were cut to their last
lines with `tail`; their counts are complete.

## Not Checked

- R4's V-TC, V-NOSERIAL, V-PREBUILT, C-CLAMP and the July control: not
  needed once V-NOMASK and V-PAD came back clean.
- Which of `main.cpp`'s masked windows dominates.
- The LED after the F4 clear.
- Heaters: the relays drive LEDs only.

## Notes

- **#81.** V-NOMASK keeps the whole loop and removes only the masked
  windows; V-PAD keeps every window and makes them rarer. Both are clean, so
  the cut-short replies come from how often the loop masks interrupts, not
  from one long window or the reply builder. A late TWI interrupt fits the
  one-bit lag seen in every bad frame. C0's 187 in 300 s (7.3 %) is above
  2026-10-31's 2 to 4 % on `d0e6ced`.
- **CRC passes.** Of the cut-short replies, the share whose 0xFF tail
  passed the CRC as an all-0xFF payload was 13 of 187 in C0, 4 of 175 in the
  closing capture, and 4 of 9 bad polls out of 186 in F4. That is far above
  the 1 in 256 that feastorg/CRUMBS#130 assumes.
- **e-stop line.** `estop_count` rose with jumper presses made on Slice_DCMT:
  the backplane carries one e-stop line to both boards.
