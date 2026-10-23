# Host tests

The firmware's own sources (`firmware/src`), built with the host C++ compiler
and run on the host. Nothing is reimplemented:

- **Real:** `main.cpp`, `rlht_handlers.cpp`, `serialCommands.cpp`, the CRUMBS
  core, the contracts headers, `PID_v1` and bread-slice-core. These come from
  PlatformIO's libdeps, so they are the versions the firmware builds with.
  bread-slice-core is built with `BSC_HOST_TEST`, so its status LED records
  the last frame (`bsc_host_led_frame`) instead of sending it.
- **Stubbed** (`stubs/`): the Arduino core, the AVR watchdog and the MAX6675
  driver. `digitalWrite()` records each pin's level (`host_pin_level`), so a
  test reads the relays; the MAX6675 stub returns `host_tc1_celsius` and
  `host_tc2_celsius`.

Frames go in through `crumbs_peripheral_handle_receive()` and replies come
back through `crumbs_peripheral_build_reply()`, the two calls the I2C
interrupts make. So a test sees what a controller on the bus would see,
including GET_ACK.

## Run

```sh
cd firmware
pio pkg install -e gen2_nano   # once, for the libraries in libdeps
make -C test/host
```

Each test is built once per PlatformIO env (`gen1_nano`, `gen2_nano`,
`gen1_nanoevery`, `gen2_nanoevery`), with that env's `RLHT_HW_GEN` and MCU
macros. The relay count and status LED then follow the same `#if` paths as on
the board. `make` exits non-zero if any check fails.

## Tests

| File | Covers |
| --- | --- |
| `test_dispatch.cpp` | Which frames are taken as commands: every opcode is swept, GET opcodes sent as commands are refused, queries answer through SET_REPLY, and another family's frame is rejected without arming or refreshing the watchdog (#36) |
| `test_estop_hold.cpp` | An e-stop press during open-loop heating: GET_STATE reports it and its count, the relays go off and the on-times to 0, a SET is refused `ESTOP`, the release is debounced and heats nothing, and a second press counts (`RLHT_CAP_ESTOP_HOLD`) |
| `test_watchdog_trip.cpp` | A command-watchdog trip: GET_WATCHDOG reports it, the relays go off, a SET is refused `WATCHDOG_TRIP`, a re-arm leaves it latched, and `CLEAR_WATCHDOG_TRIP` lifts it, keeps the count and heats nothing |
| `test_status_led.cpp` | Blue through the boot wait, then green or red on the first `loop()` pass, with the e-stop released or held from power-up; no LED driven on gen1 (#46) |

## Writing a test

- Include `host.h` for the fake clock, e-stop line, relay pins and
  thermocouples, and for `host_send()`, `host_query()`, `host_read_ack()` and
  `host_check()`.
- `millis()` advances 1 ms per `digitalRead()`, so `setup()`'s one-second
  boot wait ends after about 1000 reads. Set `host_now` to jump ahead.
- Every reply build stamps the command watchdog, as on the board, so a
  GET_STATE or GET_WATCHDOG query between an arm and the expected trip moves
  the trip.
- `setup()` and `loop()` keep state in file statics. Each case that needs a
  cold boot runs as its own process: see the `test` target in the Makefile.
- Add the file to `SINGLE` in the Makefile and a row to the table above.

Not covered here: the PID control itself, the thermocouple timing, and
anything that needs the board.
