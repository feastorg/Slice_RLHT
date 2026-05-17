# Changelog

All notable changes to Slice RLHT are summarized here.

This changelog was reconstructed from the repository history because no release
tags are currently present.

## [Unreleased]

### Added

- Added this curated root changelog, distilled from the complete Git history.

### Notes

- The root README and several docs pages still contain template placeholders.
- No formal release tags or semantic-versioned hardware/firmware releases have
  been cut in this repository yet.

## [Current State] - 2026-05-12

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

## [PlatformIO and Contract Cutover] - 2026-03-09 to 2026-04-01

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

## [Archive and Documentation Reorganization] - 2025-09-24 to 2026-02-01

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

## [CRUMBS Prototype and Firmware Revision Era] - 2025-02-19 to 2025-04-19

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

## [Initial Hardware and Firmware Bring-Up] - 2024-05-23 to 2024-09-30

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
