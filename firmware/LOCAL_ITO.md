# Local Ito speech

This profile runs English text-to-speech on StackChan with its original face.
The face stays visible while idle and opens its mouth during speaker playback.
Motor and microphone power stay off. The local profile boots directly into the
face without a launcher, menus, Home indicator or status bar. Touch input and
periodic touch reads are disabled. A boot-time FT6336 probe identifies the LCD
panel, and PMIC power-state polling continues once per second. Native hardware
power/reset controls remain intact. The normal firmware build retains its
original behavior.

## Build

From `firmware/`, with ESP-IDF 5.5.4 active:

```sh
python3 fetch_repos.py
./build_local_ito.sh
```

`repos.json` pins the Ito source. `dependencies.ito.lock` pins the local profile's
registry components; `dependencies.lock` belongs to the normal build.
The local build uses `sdkconfig.ito`, `sdkconfig.ito.defaults`,
`partitions.ito.csv` and `build-local-ito/`. The hot arena is reserved before C++ startup and must fit in internal SRAM.
Ordinary UI allocations, the frontend and console text buffers use PSRAM. The local display uses an
eight-row DMA transfer buffer.

The approved model is `ito_female_esp32s3_int4.bin`, 3,196,096 bytes, SHA256
`155da4f3c58266ab49c0b885caff08d02d72be5635d5bc960c86ed055d529b27`.
Download it according to Ito's `models/README.md`. It is not committed to Git.
The model goes in the `weights` partition at `0x9a0000`.

## Speak through USB

With the local image and model installed:

```sh
python ito/esp32/tools/stackchan.py monitor \
  --port /dev/cu.usbmodem1101 \
  --log-root build-local-ito/sessions \
  --firmware build-local-ito/stack-chan.bin
```

Use the ESP-IDF environment's Python, which provides `pyserial`. Wait for `READY`.

```text
text Hello, I am StackChan.
volume 50
text The meeting starts at half past three.
cancel
status
stats
memory
tts stop
tts probe
tts resume
text Hello again.
```

Volume starts at 35. Zero mutes playback. Cancel stops the active text job,
including queued sentences. A new text request is rejected while a job is busy.
Logs contain serial output, timestamps and image hashes in the session directory
printed by the monitor. Ctrl-C closes the monitor and releases the USB port.

USB is the local profile's control interface. The display keeps the original
breathing, blinking and speaking animation. Hardware reset returns to the face
and USB speech startup.

`memory` reports free, largest and lifetime-minimum internal/PSRAM heap bytes,
plus unused space in the 8 KiB internal USB control stack.
`tts stop` cancels playback, closes the mouth and parks inference workers.
After `OK tts stop`, the internal hot arena and PSRAM inference workspace can
be borrowed exclusively. They stay reserved, so free-heap numbers exclude them.
Worker stacks, frontend state, kernel scratch and codec DMA also remain owned.
A timeout retains ownership; retry stop to finish. A latched kernel/DMA parking
error returns `ESP_FAIL` and requires `reboot` because completion is uncertain.

`tts probe` requires a successful stop. It borrows both regions, overwrites and
checks every byte, prints their sizes, then returns ownership. It does not run
STT. `tts resume` reloads weights and rebuilds the engine in those same regions,
without recalibration or changing volume. Text commands are rejected while
stopped. `reboot` remains available while stopped or after an error.

Run `test` after resume to verify golden PCM. The dedicated PSRAM workspace
avoids repeated large heap allocations. The internal arena stays contiguous.
A borrower must return both regions and release every derived pointer before
resuming Ito.

## Flash and recovery

This profile changes the factory partition layout. It retains two 4.75 MiB OTA
slots and uses 3 MiB for assets. Its 24 KB NVS at `0x9000` matches the standalone
Ito layout and preserves the Ito split guard. Local startup does not erase NVS
on initialization errors.

A complete installation writes the matching bootloader, partition table, OTA
metadata, app, generated assets and approved model. ESP-IDF's generated flash
arguments include every item except the model. Keep the model offset and the
image set together when flashing. An app-only update requires an already matching
layout, model and assets. Full factory recovery requires the private verified
16 MB backup and its matching NVS layout.

### Boot after flashing

For this ESP32-S3 USB connection, leave download mode with a whole-chip reset,
then open the monitor without `--reset`:

```sh
python -m esptool --chip esp32s3 --port /dev/cu.usbmodem1101 \
  --before no_reset --after watchdog_reset \
  write_mem 0x6000812c 0x0 0x1
```

The masked write clears the temporary force-download bit before esptool resets
the chip. A USB-only reset can retain the cached download-mode strap on this
unit. The command requires the ROM download connection after flashing.

The [local ownership note](../.agents/notes/implemented/hardware/2026-11-14-local-ito-speech.md)
defines the codec, board and memory boundaries. The
[shared workspace measurements](https://github.com/gianpaj/ito/blob/0c2c7cd5a0163755375fb8fe6ce191320d4595e5/results/stackchan/2026-11-15-local-stt-ito/README.md) include 100 device transitions,
33 post-overwrite golden cases, safe timeout recovery and exact memory/timing
records. The runtime lends 4,891,328 bytes of PSRAM and 239,948 bytes of internal
SRAM while stopped. Both regions remain reserved and are separate from free heap.

The [original-face baseline](https://github.com/gianpaj/ito/blob/386fc611f91b07fa3fa57a4f8f0aaeb06e2ed652/results/stackchan/2026-11-15-original-face/README.md)
contains the 196-second paragraph check and compute RTF 1.0352. `degraded=1`
remains enabled. Microphone/STT and native Tailscale integration are pending.
