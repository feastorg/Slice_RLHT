---
title: Bench Records
permalink: /bench/
has_children: true
---

# Bench Records

Results of testing this Slice's firmware on real hardware, one file per bench
session.

## Writing a Record

Copy the [template](https://github.com/feastorg/bread-infra/blob/main/templates/bench-record.md)
to `docs/bench/YYYY-MM-DD-<short-slug>.md`. A record should let someone who
wasn't there tell what was tested and how far to trust it:

- the date, who ran it, the firmware commit and env, and the board and MCU;
- the rig: the controller, the link (serial or I2C), and what is on the outputs;
- each step with what you expected, what you saw, and pass, fail or skipped;
- the raw output, pasted or attached;
- what wasn't checked.

Add the record's row below in the same PR, and link it from the issue or PR it
verifies. The full guide is
[Bench Records](https://github.com/feastorg/bread-infra/blob/main/spec/slice/bench-records.md)
in bread-infra.

## Records

| Date | Record | Firmware | Boards | Result |
| --- | --- | --- | --- | --- |
| 2026-10-20 | [GET opcodes and type check](2026-10-20-get-opcodes.md) (backfill) | `c3015f1` | gen1 Nano | pass |
| 2026-10-24 | [bread-slice-core, serial burst and strict parsing](2026-10-24-core-and-serial.md) | `03089e1` | gen2 Nano, gen1 Nano Every | pass |
| 2026-10-25 | [Setpoint limits, heater faults, the lease and wide gains](2026-10-25-faults-lease-pid.md) | `236a0c2` | gen2 Nano | pass; open-thermocouple fault not reached, F3 skipped, P2 not checked; two spurious over-temperature latches |
