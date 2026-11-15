# Local Ito speech

This profile runs English text-to-speech on StackChan with its original face.
The face stays visible while idle and opens its mouth during speaker playback.
Motor and microphone power stay off. Cloud speech, Bluetooth, camera capture
and network apps are unavailable in this profile. Their menu entries show a
short message. The normal firmware build retains its original behavior.

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
```

Volume starts at 35. Zero mutes playback. Cancel stops the active text job,
including queued sentences. A new text request is rejected while a job is busy.
Logs contain serial output, timestamps and image hashes in the session directory
printed by the monitor. Ctrl-C closes the monitor and releases the USB port.

The home indicator reboots into the original launcher. AI.AGENT and AVATAR
return to the local speech face. Brightness and volume remain available in Setup.

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
