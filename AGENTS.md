# AGENTS.md

## Project

Bruce -- ESP32 offensive-security multi-tool firmware. PlatformIO + Arduino framework (C/C++). Targets ~60 board variants across ESP32, ESP32-S3, ESP32-C5, and ESP32-C6.

## Build

```sh
# Build the default env (m5stack-cardputer):
pio run

# Build a specific board:
pio run -e m5stack-cplus2

# Upload to a connected device:
pio run -e m5stack-cardputer -t upload
```

- Default env is `m5stack-cardputer` (set in `platformio.ini` `[platformio]` section).
- Board env names are in `platformio.ini` and in `boards/<board>/<board>.ini` files. Env names prefixed with `LAUNCHER_` produce a LITE_VERSION build for M5Launcher compatibility.
- **LTO is enabled** (`-flto`). `flto_prep.py` strips `-fno-lto` from link flags. The build flag `-Werror=odr` treats One Definition Rule violations as errors -- any duplicate symbol definition across translation units will fail the build.
- The post-build script `build.py` merges bootloader + partitions + app into a single `Bruce-<env>.bin` at the repo root and checks firmware size against the OTA partition.

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

Root CSV files: `custom_4Mb.csv`, `custom_4Mb_full.csv`, `custom_8Mb.csv`, `custom_16Mb.csv`. Each board `.ini` selects one via `board_build.partitions`.

### Docker

`docker-compose.yml` + `docker/Dockerfile.ci` provide a containerized build. Set `PIO_ENVS` env var to select target.

## Architecture

### Directory layout

```
src/
  main.cpp          -- setup() / loop(), global state, task creation
  core/             -- shared infrastructure (config, display, keyboard, SD, wifi, settings, serial CLI)
  modules/          -- feature modules (wifi, ble, rf, rfid, ir, fm, gps, NRF24, badusb_ble, bjs_interpreter, ...)
include/            -- global headers (globals.h, interface.h, precompiler_flags.h)
boards/<board>/     -- per-board: interface.cpp, <board>.ini, pins_arduino.h
boards/pinouts/     -- shared variant files
boards/_boards_json/ -- PlatformIO board JSON definitions
lib/                -- vendored libraries (TFT_eSPI, HAL, RTC, etc.)
embedded_resources/ -- web UI source (HTML/CSS/JS) -> compiled into webFiles.h
```

### Board support pattern

Each board has three files in `boards/<board>/`:

1. **`<board>.ini`** -- PlatformIO env config: partition table, build flags (pin definitions, feature flags like `HAS_SCREEN`, `HAS_RGB_LED`, `USB_as_HID`, display driver, etc.), and extra lib_deps.
2. **`pins_arduino.h`** -- Arduino variant pin definitions.
3. **`interface.cpp`** -- Implements `_setup_gpio()`, `_post_setup_gpio()`, `InputHandler()`, `getBattery()`, `_setBrightness()`, `powerOff()`, etc. These are weak functions in `main.cpp` that each board overrides. The `boards/_New-Device-Model/` directory is a template.

The `build_src_filter` in each board `.ini` includes `+<../boards/<board>>` so that board's `interface.cpp` is compiled into the build.

### Key globals and patterns

- Global state lives in `main.cpp` and is declared `extern` in `include/globals.h`: `bruceConfig`, `bruceConfigPins`, `tft`, `sprite`, `draw`, navigation press flags (`NextPress`, `SelPress`, `EscPress`, etc.).
- Feature availability is controlled by preprocessor flags in board `.ini` files (e.g., `HAS_SCREEN`, `LITE_VERSION`, `HAS_NS4168_SPKR`, `USB_as_HID`, `HAS_ENCODER`, `HAS_RTC`).
- `include/precompiler_flags.h` provides defaults for all optional pin/feature defines.
- `src/core/config.h` / `src/core/configPins.h` define `BruceConfig` and `BruceConfigPins` (runtime config loaded from filesystem).
- Input handling runs on a FreeRTOS task (`taskInputHandler`). Boards with rotary encoders also get a dedicated `taskEncoderPoll` task at priority 3.
- The `check(volatile bool &btn)` inline function in `globals.h` is the standard way to read button state - it suspends the input task briefly for debouncing.

### Flash size constraints

Most boards use 8MB or 16MB flash but some (CYD, older M5 devices) use 4MB. `env_4mb` restricts IR protocols and `env_light` / `LITE_VERSION` disables features (TelNet, SSH, WireGuard, interpreter, etc.) to fit. Check firmware size against the OTA partition -- the build will error if it exceeds the limit.

## Code style

- `.clang-format` in repo root: LLVM base, 4-space indent, no tabs, 110-column limit.
- Short blocks/ifs/loops allowed on single line.
- Build warnings are enabled (`-Wall` plus selected warnings). `-Wno-unused-variable` and `-Wno-narrowing` are suppressed.

## CI

- **PR check** (`PR_check.yml`): Builds a representative subset of boards (Cardputer, CPlus2, headless S3, CoreS3, CYD launcher, C5, C6) on PRs to `main`/`dev`.
- **Release build** (`buil_parallel.yml`): Builds all ~60 board variants on push to `main`/`dev` or on tags. Tagged builds create GitHub releases.
- CI requires `gcc-multilib` and `libc6-dev-i386` for mquickjs header generation.
