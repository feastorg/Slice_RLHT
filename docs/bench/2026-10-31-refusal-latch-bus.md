---
title: "2026-10-31: opcode refusal, the fault latches and cut-short replies"
parent: Bench Records
---

# 2026-10-31: opcode refusal, the fault latches and cut-short replies

**Status: complete.** U1, F1 and F4 passed. T1 found no bad thermocouple
reading. B1 found that 2 to 4 % of GET_STATE replies arrive cut short (#81).

**Verifies:** on `main` at `d0e6ced`,
[#72](https://github.com/feastorg/Slice_RLHT/pull/72) (refuse unhandled
opcodes from the handler table) and
[#71](https://github.com/feastorg/Slice_RLHT/pull/71) (latch over-temperature
after 3 readings in a row, #70), with CRUMBS 0.17.0 and
bread-crumbs-contracts 0.11.1. F1 is the open-thermocouple fault from #48 that
the 2026-10-25 record could not reach.
**By:** @CameronBrooks11

## Setup

| | |
| --- | --- |
| Firmware | `d0e6ced`, env `gen2_nano`. CRUMBS 0.17.0, bread-crumbs-contracts 0.11.1, bread-slice-core 0.1.0. F4: the same commit built with `PLATFORMIO_BUILD_FLAGS="-DRLHT_OVERTEMP_C=30 -DRLHT_SETPOINT_MAX_C=25"`, then `main` reflashed |
| Board | gen2 Nano (ATmega328P) |
| Controller | Raspberry Pi 4 Model B, I2C at 50 kHz, address 19, CRUMBS Python binding 0.16.2 and contracts 0.11.0, via the 2026-10-25 record's [`rlht_bench.py`](assets/2026-10-25-faults-lease-pid/rlht_bench.py) and this record's loggers in [`assets/`](assets/2026-10-31-refusal-latch-bus/) |
| Power | backplane (I2C and 12 V), with Slice_DCMT on the bus at address 21: its gen2 Nano Every, then its gen2 Nano for the second B1 run |
| Outputs | an LED on each relay output; two MAX6675 thermocouples at room temperature, not on a heater |

## Steps and Results

| Step | Expected | Observed | Result |
| --- | --- | --- | --- |
| B0 baseline: `version`, `caps`, `state`, `wdog`, `faults` | CRUMBS 1700, module 1.0.0; caps `0101ff7f0000`; mode 0, nothing on, no faults | as expected; `t=23.5,23.0` | pass |
| U1 query opcodes as commands (#72): `raw 0x80`, `raw 0x7F`, `raw 0x7A`, `raw 0x40`; control `raw 0x01 00` | each refused, status 2 reason 7 (UNKNOWN_OP). Before #72 a GET opcode sent as a command read APPLIED (#36). SET_MODE 0 still applies | GET_STATE, GET_CAPS, GET_FAULTS and the unassigned 0x40: status 2 reason 7. SET_MODE 0: status 1 | pass |
| T1 thermocouple log (#70): GET_STATE every 100 ms for 600 s with nobody near the rig ([`tclog.py`](assets/2026-10-31-refusal-latch-bus/tclog.py)) | no reading more than 3 C from the running median, no invalid reading, faults `0x00` | 5077 polls, medians t1 23.5 C and t2 22.8 C, maxima 24.3 and 23.8 C. The only polls more than 3 C from the median, 7 on each channel, were the 7 bad replies below, so no bad thermocouple reading. 7 polls read fault=1 with both temperatures -0.1 C, each followed by a normal one; GET_FAULTS `0x00` at the end. T1b printed those replies' bytes: `00ff...ff`, a frame cut short (B1) | pass; see B1 |
| B1 GET_STATE replies with the binding's retries off, 300 s per run ([`replylog.py`](assets/2026-10-31-refusal-latch-bus/replylog.py)) | every reply decodes | RLHT: 90 and 48 of 2553 failed to decode; 5 and 4 more passed the CRC with an all-0xFF payload. Slice_DCMT on the same bus: 1 of 2555 on its gen2 Nano (ATmega328P), 0 of 2554 on its gen2 Nano Every | fail: #81, CRUMBS#130 |
| F1 open thermocouple (#48): `mode 0`, `sp 40 0`, `watch 45`, unplug thermocouple 1; plug it back, `fclear` | within about 1 s (3 readings), fault 1, relays off, `sp` 0; faults `0x04` H1_SENSOR, not H1_OVERTEMP; `fclear` applies once it is back | t1 invalid (-3276.8) at 20:22:39.475, on-time 0 at once; fault 1 and `sp` 0 at 20:22:40.146; faults `0x04` H1_SENSOR through the 45 s. Plugged back (t1 23.3), `fclear` applied: fault 0, faults `0x00`, nothing on | pass |
| F4 3 readings over the limit (#71), test build: `sp 26 0` and `sp 25 0` in mode 0; `mode 1`, `duty 100 0`, warm thermocouple 1 by hand ([`f4log.py`](assets/2026-10-31-refusal-latch-bus/f4log.py), 100 ms polls); `fclear` while hot; `fclear` once cool | `sp 26` refused reason 4, `sp 25` applied. Latch on the third reading above 30 C, not the first: relay 1 off, faults `0x01` H1_OVERTEMP. While hot, `fclear` re-latches on the next reading. Once cool, `fclear` applies and resumes nothing | build confirmed. t1 29.8 at 20:28:12.468, 30.8 at 13.074 (relay on), 32.3 at 13.309 (relay on), 33.3 at 13.662 with H1_OVERTEMP latched, relay 1 off and on-time 0. Held to 37.8 C; `duty 0 0` refused reason 8. `fclear` at 34 C: fault 1 again at the next poll, 0.3 s later. `fclear` at 25 C: fault 0, faults `0x00`, mode 1 with on-times 0,0 | pass |
| M main reflashed: `sp 26 0` in mode 0, then `sp 0 0` | applied, the 500 C limit is back | status 1; then 0, nothing on, faults `0x00` | pass |

## Raw Output

The commands behind each step and their output:
[`raw-session.txt`](assets/2026-10-31-refusal-latch-bus/raw-session.txt).
Some long runs of unchanged lines are summarised there, each marked.

## Not Checked

- U1 on the gen1 boards, and the RLHT gen1 Nano Every in any step.
- Heater 2's over-temperature and sensor faults: host-tested only.
- That one bad reading no longer latches over-temperature: no bad reading came
  in T1's 15 minutes (T1 and T1b), so #71's single-reading case is
  host-tested only (`test_heater_faults`).
- What makes RLHT's replies get cut short (#81).

## Notes

- **Cut-short replies.** In every B1 failure, the reply is intact up to some
  byte, that byte is often corrupt (`0x80` read as `0xc0`, `e803` as `e801`),
  and every byte after it reads 0xFF. The binding's query retries hide these,
  so earlier sessions did not see them. A reply cut right after a mode byte of
  0x00 passes the CRC-8 (CRUMBS#130). It decodes as mode 0, fault, e-stop and
  both relays set, and both temperatures -0.1 C: T1's 7 fault=1 polls, and one
  in F4 at 20:28:15.427 (in mode 1; f4log.py does not print the mode byte).
  The failures do not lock to the 300 ms thermocouple reading: the Rayleigh
  statistic at the best period between 290 and 310 ms was 0.21 and 0.35 for
  the two runs, the same as at control periods of 150 to 1000 ms (0.21 to
  0.35); [`phase.py`](assets/2026-10-31-refusal-latch-bus/phase.py).
  [`crccheck.py`](assets/2026-10-31-refusal-latch-bus/crccheck.py) checks the
  CRC case.
- **One F4 poll read relay 1 off at 100 % duty** (20:28:05.493), and the next,
  135 ms later, read it on. Either a real drop or a bad reply; this data does
  not tell them apart.
- **Clearing doesn't reset the run of hot readings**, so a clear while the
  temperature is still above the limit re-latches on the next reading. The run
  never broke, so 3 in a row still holds.
- **`stamp()` in the bench scripts can step back:** 20:28:45.160 was followed
  by 20:28:45.001. It takes the seconds and the milliseconds from two
  `time.time()` calls. Times here are good to about one poll.
- The board is back on the `main` image (step M).
