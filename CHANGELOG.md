# Changelog

All notable changes to Slice RLHT are documented here.

This project did not use formal release tags through most of its history, so this changelog is organized by dated development eras rather than semantic-version releases.

## [Unreleased]

### Added

- **SET_PID takes u16 gains x1000** (`RLHT_CAP_PID_X1000`,
  bread-crumbs-contracts#64). A 12-byte SET_PID carries six u16 gains x1000,
  0 to 65.535, beside the 6-byte u8 x10 form (0 to 25.5 in steps of 0.1),
  which can be too coarse and too low for a heater. Any other length is still
  refused `MALFORMED`. Serial `R1KP=` and the like already took 0 to 65.535.
- **A command lease for the watchdog** (`RLHT_CAP_WATCHDOG_LEASE`,
  bread-crumbs-contracts#63). A 3-byte `SET_WATCHDOG` with
  `BREAD_WATCHDOG_FLAG_COMMANDS_ONLY` lets only `SET_MODE`, `SET_SETPOINTS`,
  `SET_OPEN_DUTY` and `SET_WATCHDOG` refresh it, so a poller that outlives
  the control path no longer keeps the heaters on. The 2-byte form and serial
  `WDOG=` end the lease; an undefined flag is refused and arms nothing.
- **Heater faults** (#48, `RLHT_CAP_FAULTS`). An over-temperature in either
  mode (3 readings in a row above `RLHT_OVERTEMP_C`, default 525 C, so one
  bad reading does not latch it, #70), no rise in closed loop (output
  at or above 90 % for 120 s with under 2 C of rise), and an open thermocouple
  in closed loop with a setpoint (3 readings in a row) each latch a fault that
  holds both heaters off, as a watchdog trip does. `BREAD_OP_GET_FAULTS`
  reports the `RLHT_FAULT_*` bits and GET_STATE sets `RLHT_FLAG_FAULT`;
  `SET_MODE`, `SET_SETPOINTS` and `SET_OPEN_DUTY` are refused
  `BREAD_REFUSE_FAULT` until `BREAD_OP_CLEAR_FAULTS` or serial `FAULTCLEAR`,
  which resume nothing. The thresholds are build flags and starting values,
  not measurements. Before, only a NaN reading stopped a heater, for that
  pass only: a probe off the heater or shorted kept the PID at full output.
  In `SLICE_DEBUG` builds the periodic serial state line ends with
  `FAULT:0x<bits>`.
- **Host tests** in `firmware/test/host`: the firmware's own sources built
  against the real CRUMBS core, the contracts headers and PID_v1, with the
  Arduino core, FastLED and the MAX6675 driver stubbed, once per PlatformIO
  env. They cover command dispatch, the e-stop hold, the watchdog trip and the
  boot status LED. CI runs them beside the four builds. They gate the move
  onto bread-slice-core (feastorg/.github#4).
- **Bench records** in `docs/bench/`: one file per session on real
  hardware, indexed in `docs/bench/README.md`, following bread-infra's
  `spec/slice/bench-records.md`. The first is a backfill of the 2026-10-20
  results posted on #36.
- Added this curated root changelog, distilled from the complete Git history.
- A handler for `BREAD_OP_CLEAR_WATCHDOG_TRIP` (0x7C, empty payload),
  which clears a latched command-watchdog trip and nothing else. A frame
  with a payload is rejected and the trip stays set. GET_CAPS advertises
  `RLHT_CAP_CLEAR_WATCHDOG_TRIP` beside `RLHT_CAP_CMD_WATCHDOG`
  (feastorg/Slice_DCMT#26).
- A `WDCLEAR` serial command, the local operator's clear for a watchdog
  trip. `firmware/README.md` documents the serial commands and the
  watchdog.
- `estop_count` in the GET_STATE reply: hardware e-stop presses since boot,
  wrapping at 255, appended after the 19-byte layout
  (`RLHT_STATE_OFF_ESTOP_COUNT`), so a controller sees a press it did not
  poll during. GET_CAPS advertises `RLHT_CAP_ESTOP_COUNT` (contracts 0.7.0).

### Changed

- Requires bread-crumbs-contracts `^0.11.0` (`firmware/platformio.ini` and
  `slice.yaml`), the release that defines the setpoint limits (#47), the
  heater faults (#48), the watchdog lease and the 12-byte SET_PID. It is
  additive: RLHT advertises none of the new flags yet, and the gen2 Nano and
  gen1 Nano Every images are the sizes benched on 0.10.0 (21548 and
  22147 B of flash).
- **SET_SETPOINTS is refused outside closed loop and above a ceiling**
  (#47). In open loop it is refused `WRONG_MODE`; above
  `RLHT_SETPOINT_MAX_C` (a build flag, default 500 C) it is refused
  `UNDEFINED_VALUE`, and neither setpoint is stored. Before, any i16
  deci-degree value was stored in either mode: one above the MAX6675's
  1023.75 C range could never be reached, and one sent in open loop started
  heating at the next switch to closed loop. Serial `R1TEMP=` and `R2TEMP=`
  already refused both; they now take their ceiling from the same constant.
  A negative setpoint is still accepted and is off. GET_CAPS advertises
  `RLHT_CAP_SETPOINT_LIMITS`. A controller that sent setpoints in open loop
  now sees a refusal: switch to closed loop first (closed loop at 0 is off).
- **The e-stop input, command watchdog and status LED come from
  bread-slice-core 0.1** (feastorg/.github#4). `bsc::EStop`,
  `bsc::bootWait()`, `bsc::CommandWatchdog` and `bsc::StatusLed` replace
  `pollEStop()`, the `wd*` globals, `slice.eStop`, `estopCount` and FastLED.
  The sampling, debounce, latch and colours are unchanged; what a press or a
  trip does stays here. Two differences: the boot wait restamps the watchdog
  when it ends, so an `RLHT_WATCHDOG_BOOT_MS` under a second no longer trips
  on the first loop pass, and the watchdog's timeout comparison runs inside
  the window that sets the latch.

  FastLED is gone from `lib_deps`. Gen2 flash drops by 2796 to 2897 B and
  RAM by 153 to 259 B. Gen1 has no LED; it grows by 232 to 252 B of flash
  and 3 to 5 B of RAM for the core's e-stop and watchdog, which keep the
  pin, polarity and debounce as members where the old code had constants.
- The PlatformIO platforms are pinned: `platformio/atmelavr @ 5.3.0` and
  `platformio/atmelmegaavr @ 1.10.0`, the versions local builds and CI
  already resolved. Before, a clean build took whatever platform release
  was newest, so the compiler and Arduino core could change between two
  builds of the same commit. A platform update is now a change to
  `firmware/platformio.ini`.
- Updated active firmware metadata to reference CRUMBS `0.16.0` and
  `bread-crumbs-contracts` `0.8.0`.
- Split the `relayControlLogic()` shared-state snapshot into three short
  interrupt-masked windows (worst case ~18 bytes instead of ~44). Long
  `noInterrupts()` windows delay TWI ISR entry and force a clock stretch
  that Linux SBC I2C masters mishandle — the confirmed cause of the DCMT
  query failures (feastorg/Slice_DCMT#3). Preventive hardening; RLHT
  itself measured 0 failures in bench testing.
- The CRUMBS handlers unpack every SET payload with the generated
  `rlht_*_unpack()` from `bread-crumbs-contracts` and pack the GET_STATE
  reply with `rlht_state_pack()`, instead of reading and writing
  hand-written offsets (#11). The layouts now live in one place, shared
  with the controller side. Handler behaviour and the bytes on the wire
  are unchanged; the version reply's CRUMBS version field reports 1600
  instead of 1205, as for a CRUMBS 0.16 build.
- Requires CRUMBS `^0.16.0` and `bread-crumbs-contracts` `^0.8.0`
  (`firmware/platformio.ini`), for CRUMBS 0.16.0's SET acknowledgement.
- Requires bread-crumbs-contracts `^0.10.0` (`firmware/platformio.ini` and
  `slice.yaml`), for `bread_refuse.h`, `RLHT_CAP_REFUSES`, the
  `RLHT_PERIOD_*`/`RLHT_OPEN_DUTY_MAX_PCT` ranges and
  `RLHT_CAP_ESTOP_HOLD`.
- The default I2C address is now 19 (0x13), was 10 (0x0A). 0x0A is in
  the SMBus range 0x08-0x0B that CRUMBS's `docs/protocol.md` says to
  avoid (#12). A board flashed without `-DI2C_ADR` answers at 0x13, so
  a controller that addressed 0x0A needs its address map updated.

### Fixed

- **SET_PID takes exactly 6 bytes** (#50). `rlht_set_pid_unpack` accepts
  trailing bytes, so a widened SET_PID, such as the u16 x1000 one
  bread-crumbs-contracts#64 proposes, was read as u8 x10 gains and applied.
  Any other length is now refused `MALFORMED` and changes no gain, as
  Slice_DCMT's SET_PID does. The other SET handlers still take trailing
  bytes, which the contracts keep as their extension path.
- **Serial numbers are parsed strictly** (#51), with bread-slice-core's
  `parseLong()` and `parseFloat()`. The whole token must parse and be in
  range for its field, or the command is refused with no change.
  `R1TEMP=abc` used to turn the heater off, `R1TEMP=499junk` set 499,
  `R1TC=2.9` selected thermocouple 2, `R1KP=inf` and `R1KP=1e9` were taken,
  and `WDOG=abc` disarmed the watchdog. The ranges are the ones each
  command already checked, except the gains: 0 to 65.535, the range a u16
  x1000 SET_PID would carry (bread-crumbs-contracts#64), where any value
  >= 0 was taken before. Today's u8 x10 SET_PID stops at 25.5, which can be too low for
  a relay heater whose Kp is in ms of on-time per degree, so serial keeps
  the wider range. Flash grows by about 1 KB.
- **A burst of serial input no longer holds the loop** (#39). The serial
  reader handled every buffered line in one call, and each reply blocks
  once the transmit buffer fills, so the e-stop went unsampled until the
  whole burst was answered. It now handles one line per loop pass, and the
  e-stop is sampled between lines, as Slice_DCMT does since its #49.
- **The status LED shows blue through boot again** (gen2). Since the e-stop
  sampling change (#38), the 1 s boot wait polls the e-stop, and
  `pollEStop()` also wrote the LED, so its first pass turned the boot blue
  green (or red) within microseconds. The LED update is now
  `updateStatusLed()`, called from `loop()` only: blue holds through the
  boot wait, and the first loop pass shows the e-stop state. A press during
  boot is still asserted at once; only its red waits for the boot wait to
  end.
- **A GET opcode sent as a command frame is refused** (#36). GET_STATE,
  GET_WATCHDOG and GET_CAPS are queries, answered through SET_REPLY. Sent
  as a command frame, nothing handled them, yet GET_ACK reported them
  applied. They are now refused with `BREAD_REFUSE_UNKNOWN_OP`, like any
  other opcode with no command behind it.
- **A frame typed for another family is rejected** (#36). The firmware
  never declared its type to CRUMBS, so a frame carrying DCMT's type was
  dispatched as an RLHT command, and the families' opcodes overlap. It now
  calls `crumbs_set_type_id(RLHT_TYPE_ID)`: such a frame is rejected with
  `CRUMBS_ACK_REJECT_TYPE` before any handler runs, and does not refresh
  the watchdog. Type `0x00` is still accepted. GET_ACK replies now carry
  type `0x01` instead of `0x00`.
- **Commands sent while the e-stop is held no longer heat at the release**
  (#37). The press zeroed the setpoints and on-times, but SET_MODE,
  SET_SETPOINTS and SET_OPEN_DUTY sent during the hold were stored, so
  releasing the line heated to them. While the line is held they are now
  refused with `BREAD_REFUSE_ESTOP`, which wins over
  `BREAD_REFUSE_WATCHDOG_TRIP` when both holds are in force, and the serial
  `MODE=`, `R*TEMP=` and `R*TIME=` are ignored the same way. A release
  heats nothing; resume by sending setpoints or duties. GET_CAPS advertises
  `RLHT_CAP_ESTOP_HOLD`, so the contracts pin moves to `^0.10.0`
  (`firmware/platformio.ini` and `slice.yaml`).
- **An edge train on the e-stop line can no longer postpone the stop**
  (#29). The debounce restarted on every edge, so edges less than 25 ms
  apart kept the e-stop from asserting for as long as they lasted, whatever
  the line's level. The line is now sampled on every loop pass instead of
  edge-triggered: the first LOW sample asserts the e-stop, and release
  needs 25 ms with no LOW sample. The edge interrupt is gone. The
  trade-off: a LOW glitch of any length that a loop pass samples, such as
  noise on an unpressed line, now asserts the e-stop, where a glitch
  shorter than 25 ms used to be ignored; the setpoints and on-times it
  zeroes stay zeroed until the controller sends new ones. `setup()`
  enables the pull-up first and samples the line through its 1 s boot
  wait instead of sleeping, so a line held at power-up asserts before
  `loop()` drives any relay; as before, it counts as one press in
  `estop_count`.
- **GET_ACK now reports a dropped SET as refused** (#34). Before this, no
  handler called `crumbs_refuse()`, and `on_crumbs_message`, registered as
  `on_message`, made CRUMBS count every decoded frame as applied, including
  SET_SETPOINTS while tripped and a SET_MODE the contract does not define.
  Each early return now refuses with a `BREAD_REFUSE_*` reason
  (bread-crumbs-contracts `bread_refuse.h`):
  - `MALFORMED`: a payload that does not unpack, a SET_WATCHDOG without its
    u16, a CLEAR_WATCHDOG_TRIP with a payload;
  - `WATCHDOG_TRIP`: SET_MODE, SET_SETPOINTS and SET_OPEN_DUTY while a trip
    is held;
  - `UNDEFINED_VALUE`: a SET_MODE other than open or closed loop, a
    SET_TC_SELECT with a channel other than 1 or 2, a SET_PERIODS period
    outside `RLHT_PERIOD_MIN_MS` to `RLHT_PERIOD_MAX_MS` (100-10000 ms),
    and a SET_OPEN_DUTY duty above `RLHT_OPEN_DUTY_MAX_PCT` (100). Periods
    and duties were clamped before. In each, a channel out of range keeps
    its value, the other channel still takes its own, and then the frame
    is refused;
  - `WRONG_MODE`: SET_OPEN_DUTY outside open loop;
  - `UNKNOWN_OP`: an opcode neither RLHT nor bread-common defines, refused
    in `on_crumbs_message`. Its watchdog stamp still runs first for every
    valid frame, a refused one included.

  GET_CAPS advertises `RLHT_CAP_REFUSES`. The serial `R1PERIOD=` and
  `R2PERIOD=` commands still clamp to the same range, which
  `clampRelayPeriod` now takes from the contract constants.
- A build without `RLHT_HW_GEN` now fails instead of building gen2 (#27).
  `config.h` defaulted the generation to 2 when the flag was absent, so the
  `#error "Unsupported RLHT_HW_GEN value"` in `config_hardware.h` could only
  catch an out-of-range value, never a missing one, and `pio run -e
  nano_base` produced the gen2 profile, which on a gen1 board drives the
  unconnected D5 and D7. The `gen*` environments all set the flag and are
  unaffected; `nano_base` and `nanoevery_base`, which exist only to be
  extended, now fail to build. Firmware Build checks that a build without
  the flag is rejected. Same fix as feastorg/Slice_DCMT#17.
- The PID integral survived `SET_SETPOINTS(0, 0)`, a command-watchdog trip
  and an e-stop. A heater kept running after the safe state, and resumed its
  old duty when a trip cleared or an e-stop was released, decaying only
  through Ki, over tens of seconds, and never with Ki = 0. In closed loop, a
  heater with a setpoint of 0 or less is now held off with its PID in
  manual, and a trip or an e-stop puts both PIDs in manual at once, so
  heating starts from 0 when a setpoint returns (#13). A setpoint of 0 now
  means off, where it was a 0 °C target that only sub-zero surroundings
  could heat to.
- Gen1 boards have one relay, on D6, with D7 not connected, but the gen1
  firmware ran a second heater channel on D7 and reported it in `GET_STATE`
  as if it were heating. Gen1 builds now define no `RELAY2`, never drive D7,
  and report channel 2 as off with a 0 on-time; channel-2 commands are still
  accepted (#21). `docs/hardware-revisions.md` records each generation's
  boards and pin map.
- A command could be lost when its frame landed while the main loop was
  running a heater: `relayControlLogic()` copied mode, setpoints, PID gains,
  thermocouple selects and the open-loop on-times, then wrote the copies
  back over anything the I2C handlers had written in between. On a gen1
  rig, 10 of 16 open-loop duty frames were lost this way, `set_open_duty_pct
  0/0` among them, so a controller's safe state did not stop the heater.
  The main loop now only reads those fields; the PIDs work on their own
  variables, and the closed-loop output is published only while the mode is
  still closed loop (#14).
- Entering closed loop seeded the PID from the open-loop on-time, so a
  heater resumed the open-loop duty and, with Ki = 0, stayed there. Closed
  loop now starts from 0, and the PID switches to automatic only with a
  fresh input, so a non-zero Kd no longer adds a one-sample kick from the
  last closed-loop reading (#17).
- Entering open loop kept the PID's last on-time as the open-loop duty: the
  heater kept running with no setpoint to stop it, and `SET_SETPOINTS(0, 0)`
  did nothing in open loop. `SET_MODE` and serial `MODE=` now start open
  loop at 0; re-sending open loop while already in it keeps the duty.
- A command-watchdog trip now latches until an explicit clear (#9). Any
  valid command frame used to clear it, so a heater command could release
  a hold that a real bus failure had set; `SET_WATCHDOG` and every serial
  line did too. Now only `BREAD_OP_CLEAR_WATCHDOG_TRIP`, the serial
  `WDCLEAR` command or a reboot clears it. Command frames, replies,
  `SET_WATCHDOG`, `WDOG=<ms>` and other serial lines still keep the
  watchdog fed. While the trip is set, `SET_MODE`, `SET_SETPOINTS` and
  `SET_OPEN_DUTY`, and the serial `MODE=`, `R1TEMP=`, `R2TEMP=`,
  `R1TIME=` and `R2TIME=` commands, are ignored, so nothing sent during
  a trip is acted on when it is cleared. Clearing resumes nothing: the
  setpoints and on-times stay at 0 until a new setpoint or duty arrives,
  and a PID then starts from an on-time of 0. A controller that
  predates the clear has no remote way to clear a trip on this
  firmware.

### Notes

- The root README still contains template placeholder sections.
- No formal release tags or semantic-versioned hardware/firmware releases have
  been cut in this repository yet.

## [2026-04-01 to 2026-05-12] Firmware Hardening And Dependency Cleanup

### Added

- Active PlatformIO firmware project under `firmware/`, with separate `include/`
  and `src/` trees.
- CRUMBS-based RLHT firmware using the published `bread-crumbs-contracts`
  package.
- PlatformIO environments for gen1/gen2 hardware on Arduino Nano and Nano Every:
  - `gen1_nano`
  - `gen2_nano`
  - `gen1_nanoevery`
  - `gen2_nanoevery`
- Dual-relay control firmware with:
  - closed-loop PID temperature control
  - open-loop relay on-time control
  - dual MAX6675 thermocouple reads
  - configurable thermocouple-to-relay selection
  - configurable relay periods
  - serial command interface
  - CRUMBS handlers and replies for mode, setpoints, PID, periods, thermocouple
    selection, open-loop duty, state, version, and capabilities
- Emergency-stop handling with internal pull-up input mode, debounce, relay
  shutdown, and status LED behavior on supported hardware.
- AVR/megaAVR watchdog support with a 1 second timeout.
- Static serial command buffer to avoid heap allocation from Arduino `String`.
- KiCad hardware project under `hardware/`, including hierarchical sheets for
  relay, thermistor, and MAX31855K circuitry.
- Checked-in fabrication outputs under `hardware/outputs/` plus
  `hardware/outputs.zip`.
- KiBot hardware automation config and Makefile targets for ERC, DRC,
  fabrication outputs, BOMs, drill files, position files, and board renders.
- GitHub Pages-style docs scaffold under `docs/`, including generated board
  render images.
- Historical hardware and firmware archives under `archive/`.

### Changed

- Moved the active firmware from sketch-era `.ino` layouts to a PlatformIO C++
  project.
- Switched from local contract header copies to published BREAD/RLHT contracts.
- Made I2C address, hardware generation, and debug behavior configurable through
  build flags.
- Centralized relay period writes and tightened relay period types.
- Mapped local control modes to contract-defined enums.
- Changed firmware version reporting to use the canonical contract version.
- Updated timing fields to align with Arduino `millis()` semantics.
- Updated docs automation to use lowercase `feastorg` organization casing.

### Fixed

- Reduced e-stop race and bounce issues with debounce and interrupt-guarded
  shared state.
- Reduced heap-fragmentation risk in serial command parsing by removing Arduino
  `String` input handling.
- Guarded serial command trimming against empty input.
- Reduced thermocouple timing drift by using relative interval updates.
- Replaced raw invalid-temperature sentinel handling with the protocol-defined
  `BREAD_INVALID_I16`.
- Avoided repeated `FastLED.show()` calls when the status LED state has not
  changed.
- Removed a no-op relay-control path.

### Removed

- Removed drift-prone local copies of BREAD/RLHT contract headers after the
  published contract cutover.
- Removed remaining active hardware backup zip files from the main hardware
  tree.

## [2026-03-09 to 2026-04-01] PlatformIO And Contract Cutover

### Added

- Clean PlatformIO firmware layout with:
  - `firmware/platformio.ini`
  - `firmware/include/config.h`
  - `firmware/include/config_hardware.h`
  - `firmware/include/globals.h`
  - `firmware/src/main.cpp`
  - `firmware/src/rlht_handlers.cpp`
  - `firmware/src/serialCommands.cpp`
  - `firmware/src/printSerialOutputRLHT.cpp`
- Initial local BREAD/RLHT contract headers during the migration.
- Published contract dependency usage after the migration stabilized.
- Capability reply support.
- Build profiles for Nano and Nano Every, split by RLHT hardware generation.
- Firmware workspace ignores and VS Code extension recommendations.

### Changed

- Archived the previous gen2 firmware before replacing the active firmware with
  the new CRUMBS-based implementation.
- Added hardware-generation selection to firmware build configuration.
- Added build-flag based I2C address selection.
- Moved CRUMBS debug behavior into build configuration.
- Internalized CRUMBS context handling.
- Cleaned up serial output and command string handling.
- Bumped CRUMBS to `0.12.0` and `bread-crumbs-contracts` to `0.4.1`.

### Fixed

- Added development-context and version checks during the contract migration.
- Debounced e-stop handling and switched the e-stop input to internal pull-up.
- Centralized relay period writes and tightened unsigned period handling.
- Reduced timing drift in thermocouple reads.
- Switched invalid temperature serialization to the protocol-defined invalid
  value.

### Removed

- Removed temporary local contract headers once published packages were in use.
- Removed temporary firmware placeholder files after the active PlatformIO tree
  landed.

## [2025-09-24 to 2026-02-01] Archive And Documentation Reorganization

### Added

- GitHub Actions docs pipeline using shared `bread-infra` workflows.
- Docs scaffold under `docs/`, including architecture, testing, changelog, TODO,
  KiBot, and index pages.
- Generated board render images and generated KiBot index page.
- `hardware/config.kibot.yaml` for KiBot output generation.
- `hardware/Makefile` for hardware validation and fabrication-output targets.
- Top-level hardware archive organization, later consolidated under
  `archive/hw_archive/`.
- Legacy `RLHT_R1` sketch in the firmware archive.

### Changed

- Moved the BOM spreadsheet from `documentation/` to `docs/`.
- Renamed generated hardware output paths from `Gerbers/` to `outputs/`.
- Renamed `Gerbers.zip` to `outputs.zip`.
- Split firmware into generation-oriented gen1/gen2 layouts before the later
  PlatformIO cutover.
- Rearranged archives so historical firmware lives under `archive/fw_archive/`
  and historical hardware lives under `archive/hw_archive/`.
- Revised the active KiCad hardware against newer template/library expectations.

### Removed

- Removed old active hardware backup zips that existed remotely.
- Removed older duplicate archive locations after the `archive/` layout was
  established.
- Removed an obsolete hardware reference image and archived binary backup zips.

## [2025-02-19 to 2025-04-19] CRUMBS Prototype And Firmware Revision Era

### Added

- Multiple firmware revision snapshots, including R2 new, R2.1, R2.2, and older
  R2/R3 code paths.
- Historical hardware snapshots for OSF dates and a Finn-derived recent design.
- Notes describing known hardware progression and lab board context.
- Experimental CRUMBS-based firmware sketch.
- Firmware modules for message handling, request handling, serial output, and
  relay actuation.

### Changed

- Renamed and organized firmware revisions as development moved from R2 new to
  R2.1/R2.2 and then toward a CRUMBS-based active firmware.
- Moved older firmware revisions into legacy folders.
- Refactored the active firmware away from a single monolithic sketch.
- Moved debug macros into configuration.
- Updated active and archived KiCad files during hardware/library maintenance.

### Fixed

- Fixed e-stop ISR behavior in the CRUMBS firmware prototype.
- Separated e-stop processing from the main loop.
- Modularized relay actuation.

## [2024-05-23 to 2024-09-30] Initial Hardware And Firmware Bring-Up

### Added

- Initial GPL-licensed repository with README, KiCad board, schematic, project
  files, and early BOM artifacts.
- Initial generated board-house outputs, including copper, mask, paste,
  silkscreen, edge cuts, drill files, and Gerber job data.
- Early Arduino firmware sketches for `RLHT_R2` and `RLHT_R3`.
- JLCPCB Rev-A order package and generated Gerber artifacts.
- Hierarchical KiCad sheets for MAX31855K thermocouple, relay, and thermistor
  circuitry.

### Changed

- Revised the KiCad board and schematic heavily during terminal and review
  updates.
- Reorganized generated fabrication outputs during the Rev-A preparation.

### Removed

- Removed the initial placeholder firmware file once real firmware sketches were
  added.
- Removed an early temporary BOM artifact during hardware review cleanup.
