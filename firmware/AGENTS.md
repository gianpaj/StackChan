# Firmware Agent Guide

## Scope
This file applies to everything under `firmware/`.

## Project identity
- This subtree is a custom ESP32 firmware for **M5Stack StackChan**, derived from this repository's `main` branch.
- The default target is **ESP32-S3** with `CONFIG_BOARD_TYPE_M5STACK_STACK_CHAN=y` and `CONFIG_LANGUAGE_EN_US=y` in `sdkconfig.defaults`.
- The firmware is built with **ESP-IDF v5.5.4**.
- Hardware reference: <https://docs.m5stack.com/en/StackChan>

## High-level architecture
- `main/main.cpp` boots the local UI shell, initializes HAL, installs Mooncake apps, and then hands control to either the Xiaozhi runtime or the OpenAI runtime.
- `main/apps/` contains the local app launcher experience and app-specific UI flows.
- `main/hal/` contains board integration, services, runtime bridges, BLE, networking, MCP tool wiring, and OpenAI/Xiaozhi handoff logic.
- `main/stackchan/` contains avatar, motion, animation, modifiers, and StackChan-specific behavior.
- `main/hal/board/config.h` is the primary local hardware pin/config reference for this board.
- `xiaozhi-esp32/` is a fetched upstream dependency that provides most of the assistant runtime.
- `patches/xiaozhi-esp32.patch` is the local customization layer for upstream Xiaozhi code.

## Directory ownership and edit strategy
### Prefer editing here
- `main/`
- `sdkconfig.defaults`
- `partitions.csv`
- `repos.json`
- `patches/`
- `fetch_repos.py`

### Treat as upstream or generated
- `xiaozhi-esp32/` is vendor-style upstream code fetched from `repos.json`.
- `managed_components/` is ESP-IDF component manager output.
- `components/` contains fetched dependencies.
- `dependencies.lock` is dependency metadata, not hand-authored application logic.

### Rules for upstream dependencies
- Do **not** make casual one-off edits inside `xiaozhi-esp32/` or other fetched dependencies.
- If you must change upstream code, keep the change rebase-friendly and record it in `patches/xiaozhi-esp32.patch` so `python3 ./fetch_repos.py` can reproduce the workspace.
- If an upstream version changes, update `repos.json` and verify the patch still applies cleanly.

## Build and setup workflow
Run commands from `firmware/`.

1. Fetch dependencies:
   - `python3 ./fetch_repos.py`
2. Build:
   - `idf.py build`
3. Flash if needed:
   - `idf.py flash`
4. Monitor if needed:
   - `idf.py monitor`

## Configuration guidance
- Prefer durable defaults in `sdkconfig.defaults`.
- Avoid noisy, unrelated churn in `sdkconfig`; only commit it when a concrete config change is intentional.
- The default partition table uses two OTA app slots plus a dedicated `assets` partition. Be careful when changing flash layout.
- Keep board assumptions aligned with the current StackChan hardware: 320x240 display, ESP32-S3, 16 MB flash, PSRAM enabled.

## Secrets and credentials
- Never commit API keys, Wi-Fi credentials, tokens, or device secrets.
- OpenAI support reads the API key from:
  - build-time `CONFIG_OPENAI_API_KEY`, or
  - NVS namespace `openai`, key `api_key`
- If you need a key for local testing, inject it at build time or provision it into NVS. Do not hardcode it in source, `sdkconfig.defaults`, or docs.

## Coding expectations
- Follow the existing C++ + ESP-IDF style and the local `.clang-format`.
- Keep changes small, explicit, and easy to rebase on top of upstream `main`.
- Use existing logging conventions: `mclog::...` in local app/UI code and `ESP_LOG...` where that is already the surrounding style.
- Prefer fixing root causes over adding workaround state.
- Keep hardware-facing code conservative; unexpected servo, motor, display, or power behavior can damage hardware or degrade UX.

## LVGL, tasks, and concurrency
- Any LVGL work done from callbacks, worker tasks, or non-UI control paths should follow the existing locking pattern with `LvglLockGuard`.
- Do not block the main update loops with long network or device operations.
- Prefer background tasks/callbacks for I/O, then marshal state back into the UI safely.
- Be careful with stack size and heap use; this firmware already mixes graphics, networking, audio, BLE, camera, and AI runtimes.

## App integration notes
- New launcher apps usually require updates in both:
  - `main/apps/apps.h`
  - `main/main.cpp`
- When an app subscribes to HAL signals or allocates UI resources, clean them up on close.
- Keep StackChan behavior consistent across both assistant paths when possible:
  - Xiaozhi path in local HAL bridge / upstream runtime integration
  - OpenAI path in `main/hal/hal_openai_webrtc.cpp`

## Hardware safety notes
- Avoid aggressive motion defaults, constant servo torque, or behavior that can fight manual movement.
- Preserve safe behavior around head motion, torque release, battery handling, and charging.
- Do not assume optional peripherals are always available; fail gracefully if camera, audio, BLE, or network init is unavailable.

## Validation expectations
Before finishing a meaningful firmware change, prefer to do at least the following when possible:
- `python3 ./fetch_repos.py` if dependency state may matter
- `idf.py build`
- `idf.py flash monitor` for hardware-facing changes when a device is available

If you cannot run a validation step, say so clearly in your handoff.

## Good handoff checklist
When you finish a change, mention:
- what you changed
- whether you touched local code, upstream patching, or config
- what you verified
- any remaining risks, especially around hardware behavior, memory usage, or upstream patch drift
