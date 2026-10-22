---
title: "2026-10-20: GET opcodes and type check"
parent: Bench Records
---

# 2026-10-20: GET opcodes and type check

**Backfill.** This record was written on 2026-10-21, from results first
posted as an issue comment on 2026-10-20
([#36 comment](https://github.com/feastorg/Slice_RLHT/issues/36#issuecomment-6302819636)).
It summarises that comment, which holds the original wording; it adds no new
results.

**Verifies:** [#36](https://github.com/feastorg/Slice_RLHT/issues/36) (fixed by
[#41](https://github.com/feastorg/Slice_RLHT/pull/41)).

## Setup

| | |
| --- | --- |
| Firmware | `c3015f1` (the #41 fix plus a PlatformIO platform pin), CRUMBS 0.16.2 in the version reply |
| Board | gen1 Nano RLHT |
| Controller | CRUMBS Python binding, `send_confirmed`, over I2C |
| Outputs | not recorded |

## Steps and Results

| Frame | Expected | Observed | Result |
| --- | --- | --- | --- |
| GET_STATE 0x80, GET_WATCHDOG 0x7D, GET_CAPS 0x7F as command frames | refused UNKNOWN_OP | REFUSED UNKNOWN_OP (0x07); on `cc7d9d5` the same frames read APPLIED | pass |
| opcode 0x01 typed 0x02 (DCMT) | rejected, TYPE | REJECTED, reason TYPE | pass |
| SET_WATCHDOG 500 typed 0x02 | rejected, TYPE; watchdog unchanged | REJECTED, reason TYPE; GET_WATCHDOG stayed disarmed, timeout 0 | pass |
| the suite's other RLHT cases | as on `cc7d9d5` | as on `cc7d9d5`; queries through SET_REPLY answered as before | pass |

## Not Checked

- The status LED on gen2 RLHT boards was not part of this session.
- The raw controller output was not kept with the comment.
