# AGENTS.md

<!-- codebase-memory-mcp:start -->
## Codebase Knowledge Graph (codebase-memory-mcp)

This project uses codebase-memory-mcp to maintain a knowledge graph of the codebase.
ALWAYS prefer MCP graph tools over grep/glob/file-search for code discovery.

### Priority Order
1. `search_graph` — find functions, classes, routes, variables by pattern
2. `trace_path` — trace who calls a function or what it calls
3. `get_code_snippet` — read specific function/class source code
4. `query_graph` — run Cypher queries for complex patterns
5. `get_architecture` — high-level project summary

### When to fall back to grep/glob
- Searching for string literals, error messages, config values
- Searching non-code files (Dockerfiles, shell scripts, configs)
- When MCP tools return insufficient results

### Examples
- Find a function: `search_graph(name_pattern=".*audio_test.*")`
- Who calls it: `trace_path(function_name="audio_test", direction="inbound")`
- Read source: `get_code_snippet(qualified_name="main/src/player.audio_test")`
<!-- codebase-memory-mcp:end -->

## Workflow
- This is an ESP-IDF firmware project. Source the ESP-IDF environment first, then run `idf.py` from the repo root.
- Primary verification for the main firmware is `idf.py build`.
- Change Kconfig settings through `idf.py menuconfig`; `sdkconfig` is generated and `.gitignore` excludes `sdkconfig.*`.
- **NEVER modify files directly unless the user explicitly asks you to.** When proposing changes, describe them first and wait for confirmation before editing.
- **NEVER commit or `git commit` unless the user explicitly asks you to.** Do not stage (`git add`) or commit code on your own initiative.

## Project Shape
- Root `CMakeLists.txt` enables `CMAKE_EXPORT_COMPILE_COMMANDS` and `idf_build_set_property(MINIMAL_BUILD ON)`.
- `main/CMakeLists.txt` currently builds both `main/src/main.c` and `main/src/player.c`.
- The current default app flow is not the old sensor/UI demo: `app_main()` in `main/src/main.c` calls `bsp_init()`, unmutes audio, and then runs `audio_test()`; the SD-card, I2C, UI, and AHT20 path is present but commented out.
- `audio_test()` in `main/src/player.c` is the active wiring example for the `amp` component: it connects a sine PCM reader to the I2S writer through `amp_controller`.
- Major local components: `components/bsp` for board pins, SD card, and audio mute; `components/amp` for the audio pipeline; `components/ui` for OLED/button code; `components/sensor` for AHT20; `components/sds` for the bundled string library.
- `components/utils` is header-only right now; its `CMakeLists.txt` registers no source files.

## Test Workflow
- The focused test app for the audio pipeline is `test_apps/amp`, not the main firmware.
- `test_apps/amp/CMakeLists.txt` sets `EXTRA_COMPONENT_DIRS "../../components"`, limits `TEST_COMPONENTS` to `amp`, and bakes `test_apps/amp/test_assets` into a flash FATFS image with `fatfs_create_spiflash_image(...)`.
- `components/amp/test/` contains the Unity tests that the `test_apps/amp` app builds and runs.

## Hardware And Config Gotchas
- `sdkconfig` currently targets `esp32s3`; `dependencies.lock` resolves ESP-IDF `5.5.4`, while `main/idf_component.yml` only requires `idf >=5.4.0`.
- The active SD-card mode is SPI, not SDIO: `CONFIG_SD_CARD_SPI_MODE=y` in `sdkconfig`. This matches the README note that SDIO is unreliable on this board.
- `bsp_sd_card_init()` mounts `/sdcard` with `format_if_mount_failed = true`, so a failed mount can trigger a reformat.
- Board pin assignments are centralized in `components/bsp/bsp.h`; check there before changing GPIO usage.

## Generated And Vendored Files
- Avoid manual edits in `build/`, `managed_components/`, `dependencies.lock`, and `sdkconfig*` unless the task is specifically to regenerate them.
- `test_apps/amp/build/` and `test_apps/amp/managed_components/` are also generated.
- `components/ui/u8g2/` is vendored third-party code. Prefer changes in `components/ui/u8g2_port.c` or `components/ui/ui.c` unless a vendor patch is unavoidable.
