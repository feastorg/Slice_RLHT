---
title: "Hardware Revisions"
description: "Board generations, which archived boards they are, and their pin maps."
---

Which board generation is which, and how their pins differ.

## Generations

| | Board files | Relays | Thermocouples | Firmware profile |
|---|---|---|---|---|
| **Gen1** | `archive/hw_archive/osf_2022-05-19`, `osf_2022-06-30`, `osf_2023-02-01`, `osf_2023-12-06` | 1 | 2× MAX6675 | `gen1_nano`, `gen1_nanoevery` |
| **Gen2** | `archive/hw_archive/finn_recent` | 2 | 2× MAX6675 | `gen2_nano`, `gen2_nanoevery` |
| — | `hardware/` (current KiCad) | 2 | 2× MAX31855K | **None** (#22) |

`hardware/` is not gen2. It uses MAX31855K converters, which the firmware's
MAX6675 driver cannot read, and routes the thermocouple pins differently.

## Pin maps

Each net below is the Nano `A1` pad's net, read out of the PCB.

| Arduino pin | Gen1 | Gen2 | `hardware/` |
|---|---|---|---|
| D2 | `/E_STOP` | `/E_STOP` | `/E_STOP` |
| D5 | — | `/LED` | `/LED` |
| D6 | `/PWM` (relay K1) | `/Relay1` | `/Relay1` |
| D7 | — | `/Relay2` | `/Relay2` |
| D8, D9 | — | — | — |
| D10 | `/SS1` | `/SS1` | `/TC_CLK` |
| D11 | `/SS2` | `/SS2` | `/SS2` |
| D12 | `/TC_DATA` | `/TC_DATA` | `/TC_DATA` |
| D13 | `/TC_CLK` | `/TC_CLK` | `/SS1` |

— is unconnected.

## Gen1 has one relay

Gen1 has one relay, K1, driven from D6. D7 is not connected. Both thermocouple
channels are populated.

The gen1 firmware profile sets `RLHT_RELAY_COUNT` to 1 and does not define
`RELAY2`, so D7 is never driven. Channel-2 commands are still accepted, so the
wire format is the same on both generations, but they drive nothing:
`GET_STATE` never reports `RLHT_FLAG_RELAY2_ON` or a channel-2 on-time. A
channel can still read either thermocouple through `SET_TC_SELECT`.

`GET_CAPS` is the same on both generations, so a host cannot tell them apart
over the bus.

Gen1 has no status LED either: D5 is unconnected, and the gen1 profile compiles
the LED out (`RLHT_HAS_STATUS_LED`).
