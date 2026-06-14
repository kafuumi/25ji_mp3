# AGENTS.md

## Workflow
- This is an ESP-IDF firmware project, not a general CMake app. Source the ESP-IDF environment before running project commands, then use `idf.py` from the repo root.
- Primary verification is `idf.py build`. Use `idf.py flash monitor` only when hardware access is intended.
- Use `idf.py menuconfig` to change project settings; `sdkconfig` is generated output and `.gitignore` excludes `sdkconfig.*`.

## Project Shape
- Root `CMakeLists.txt` boots ESP-IDF and enables `CMAKE_EXPORT_COMPILE_COMMANDS`; `main/src/main.c` is the actual app entrypoint.
- `main/CMakeLists.txt` only registers `main/src/main.c`, so other files under `main/src/` are not built unless added there.
- Major local components live in `components/`: `bsp` for board and SD card wiring, `sensor` for AHT20, `ui` for the OLED/button layer, and `sds` for the bundled string library.

## Hardware And Config Gotchas
- `sdkconfig` currently targets `esp32s3`; `dependencies.lock` resolves ESP-IDF `5.5.4`, while `main/idf_component.yml` requires `idf >=5.4.0`.
- The active SD-card mode is SPI, not SDIO: `CONFIG_SD_CARD_SPI_MODE=y` in `sdkconfig`, matching the README note that SDIO is unreliable on this board.
- `bsp_sd_card_init()` mounts `/sdcard` and sets `format_if_mount_failed = true`, so failed mounts may reformat the card.

## Code Reading Shortcuts
- Start execution tracing at `main/src/main.c`: startup does SD card init, I2C bus init, OLED UI init, then loops on AHT20 sensor reads.
- Board pin assignments are centralized in `components/bsp/bsp.h`; check there before changing GPIO usage.
- `components/ui/u8g2/` is vendored third-party code. Prefer changing `components/ui/u8g2_port.c` or `components/ui/ui.c` unless a vendor patch is truly required.

## Generated And Vendored Files
- `build/`, `managed_components/`, `dependencies.lock`, and `sdkconfig*` are generated or dependency-managed artifacts; avoid manual edits unless the task specifically requires regenerating them.
