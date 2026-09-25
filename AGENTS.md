# AGENTS.md

## Project

Bruce -- ESP32 offensive-security multi-tool firmware. PlatformIO + Arduino framework (C/C++).
**This fork is evrahim-s3 only**: a single custom ESP32-S3 board (ST7789 TFT + XPT2046 touch +
CC1101 + NRF24). All other upstream board variants were stripped from the tree.

## Build

```sh
# Build the only env (evrahim-s3):
pio run
pio run -e evrahim-s3

# Upload to a connected device:
pio run -e evrahim-s3 -t upload
```

- The only env is `evrahim-s3` (declared in `boards/evrahim-s3/evrahim-s3.ini`, included via
  `extra_configs` globs; also the `[platformio]` `default_envs`).
- There are no `LAUNCHER_*` / `LITE_VERSION` envs in this fork.
- **LTO is enabled** (`-flto`). `flto_prep.py` strips `-fno-lto` from link flags. The build flag `-Werror=odr` treats One Definition Rule violations as errors -- any duplicate symbol definition across translation units will fail the build.
- The post-build script `build.py` merges bootloader + partitions + app into a single `Bruce-evrahim-s3.bin` at the repo root and checks firmware size against the OTA partition.

### Pre-build scripts (run automatically via `extra_scripts`)

| Script | Purpose |
|---|---|
| `patch.py` | Weakens `ieee80211_raw_frame_sanity_check` in `libnet80211.a`; also gzips + embeds `embedded_resources/web_interface/` files into `include/webFiles.h` |
| `pre_build_current_year.py` | Generates `include/current_year.h` |
| `gen_mqjs_headers.py` | Compiles mquickjs JS stdlib into C headers in `lib/mquickjs_headers/`. Requires a **32-bit host GCC** (`gcc -m32`). CI installs `gcc-multilib` and `libc6-dev-i386` for this. |
| `patch_library_conflicts.py` | Renames conflicting symbols in third-party libs (ESP8266SAM, JPEGDecoder, etc.) |
| `flto_prep.py` | Removes `-fno-lto` from link flags to enable LTO |

### Generated files (gitignored, do not edit)

- `include/webFiles.h` -- generated from `embedded_resources/web_interface/`
- `include/current_year.h`
- `lib/mquickjs_headers/mqjs_stdlib_generator[.exe]`

### Partition tables

Only `custom_16Mb.csv` remains at the root; `boards/evrahim-s3/evrahim-s3.ini` selects it via
`board_build.partitions`. (Upstream's 4Mb/8Mb tables were deleted with the other boards.)

## Architecture

### Directory layout

```
src/
  main.cpp          -- setup() / loop(), global state, task creation
  core/             -- shared infrastructure (config, display, keyboard, SD, wifi, settings, serial CLI)
  modules/          -- feature modules (wifi, ble, rf, rfid, ir, fm, gps, NRF24, badusb_ble, bjs_interpreter, ...)
include/            -- global headers (globals.h, interface.h, precompiler_flags.h)
boards/evrahim-s3/  -- the board: interface.cpp, evrahim-s3.ini, pins_arduino.h
boards/pinouts/     -- Arduino variant dispatcher (evrahim-s3 only, errors otherwise)
boards/_boards_json/ -- PlatformIO board JSON definition (evrahim-s3.json only)
lib/                -- vendored libraries (TFT_eSPI, HAL, RTC, etc.)
embedded_resources/ -- web UI source (HTML/CSS/JS) -> compiled into webFiles.h
```

### Board support pattern

`boards/evrahim-s3/` holds the three board files:

1. **`evrahim-s3.ini`** -- PlatformIO env config: partition table, build flags (pin definitions, feature flags like `HAS_SCREEN`, `USB_as_HID`, display driver, etc.), and extra lib_deps.
2. **`pins_arduino.h`** -- Arduino variant pin definitions.
3. **`interface.cpp`** -- Implements `_setup_gpio()`, `_post_setup_gpio()`, `InputHandler()`, `getBattery()`, `_setBrightness()`, `powerOff()`, etc. These are weak functions in `main.cpp` that the board overrides.

The `build_src_filter` in `evrahim-s3.ini` includes `+<../boards/evrahim-s3>` so the board's `interface.cpp` is compiled into the build.

### Key globals and patterns

- Global state lives in `main.cpp` and is declared `extern` in `include/globals.h`: `bruceConfig`, `bruceConfigPins`, `tft`, `sprite`, `draw`, navigation press flags (`NextPress`, `SelPress`, `EscPress`, etc.).
- Feature availability is controlled by preprocessor flags in the board `.ini` (e.g., `HAS_SCREEN`, `LITE_VERSION`, `HAS_NS4168_SPKR`, `USB_as_HID`, `HAS_ENCODER`, `HAS_RTC`).
- `include/precompiler_flags.h` provides defaults for all optional pin/feature defines.
- `src/core/config.h` / `src/core/configPins.h` define `BruceConfig` and `BruceConfigPins` (runtime config loaded from filesystem).
- Input handling runs on a FreeRTOS task (`taskInputHandler`). Boards with rotary encoders also get a dedicated `taskEncoderPoll` task at priority 3.
- The `check(volatile bool &btn)` inline function in `globals.h` is the standard way to read button state - it suspends the input task briefly for debouncing.

### Flash size constraints

evrahim-s3 has 16MB flash (`custom_16Mb.csv`, no OTA slots -- updates go over USB). Check firmware size
against the app partition -- the build will error if it exceeds the limit.

## Code style

- `.clang-format` in repo root: LLVM base, 4-space indent, no tabs, 110-column limit.
- Short blocks/ifs/loops allowed on single line.
- Build warnings are enabled (`-Wall` plus selected warnings). `-Wno-unused-variable` and `-Wno-narrowing` are suppressed.

## CI

- **Build evrahim-s3** (`.github/workflows/build-evrahim-s3.yml`): builds the single `evrahim-s3` env on
  every push/PR and uploads `Bruce-evrahim-s3.bin` as an artifact. Upstream's multi-board workflows
  (`PR_check.yml`, `buil_parallel.yml`, etc.) were removed with the other boards.
- CI requires `gcc-multilib` and `libc6-dev-i386` for mquickjs header generation.
- `tools/run_firmware_build.sh` triggers the workflow on demand and downloads the resulting `.bin`.
