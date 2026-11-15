# Agent Note: Touch-free local speech profile

Status: implemented — The local image is flashed and passes serial speech checks.

## Problem

The local speech profile needs the original face and USB controls. Menu code,
touch polling and touch widgets consume resources without serving that mode.

## Decision

The local profile boots directly into the original face. Its CMake source list
excludes menu apps and inactive MCP integration. HAL creates no LVGL touch input.
The board keeps FT6336 panel identification and polls PMIC state once per second.
The face keeps breathing, blinking and speaking modifiers, without Home/status
widgets, head-pet reactions or IMU reactions. Stored warm targets cannot enter
menus. The normal profile retains its original source list and controls.

## Alternatives considered

Keeping disabled menu entries retains unused code and allocations. A replacement
renderer would change the face and add work outside this memory reduction.
New button gestures do not help remove the unused touch/menu path.

## Consequences

The source-pruned app is 1,988,128 bytes, SHA256
`f8019354d56648876608b1a68172148d6ba08e78f6dfdc058e62333be8241179`.
Static DIRAM is 87,835 bytes, down from the saved 108,163-byte baseline.
Post-face free internal SRAM is 87,635 bytes, with the 239,948-byte hot reservation
still held. Post-face PSRAM is 8,295,108 bytes. Warm internal free memory is about
34.4 KB, compared with 13.2 KB in the saved baseline.

Local and normal builds pass. The local compile database contains no app sources;
the ELF contains no launcher, menu, Home/status widget or touch-polling symbols.
Golden boot PCM matches `363d7d98`; quiet numbers match `c4472e58`. The greeting
has zero underruns. Cancel flushes in 18.6 ms. No touch-poll warnings appear.
Private images, ELF/map, configuration, size report and serial results are retained
outside the build directory. The LCD, codec ownership and inference arithmetic
are unchanged. Native power/reset code is unchanged; physical off/on, battery
operation and download-button behavior are not re-tested in this check.
The user confirms mouth movement during speech, closure afterward and no tap response.

[Local speech ownership](../hardware/2026-11-14-local-ito-speech.md) records
the codec, model, partition and safety boundaries. Shared runtime memory has its
own lifecycle decision and measurements.
