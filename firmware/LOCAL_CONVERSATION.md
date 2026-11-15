# Local speech through USB

This diagnostic profile recognizes English with Oido v2b and speaks with Ito.
Both models run on StackChan. USB supplies commands and prints the transcript.
The original face stays visible and animates during speaker playback. Capture
and playback are separate phases. The microphone is powered only for capture;
motors stay off. Physical power/reset buttons keep their existing behavior.

## Build

Activate ESP-IDF 5.5.4, then run from `firmware/`:

```sh
python3 fetch_repos.py
./build_conversation.sh build
```

The build uses `sdkconfig.conversation`, `build-conversation/` and
`dependencies.conversation.lock`. The USB TTS profile remains
`./build_local_ito.sh`; the normal firmware remains `idf.py -B build build`.
`repos.json` pins both engines and applies the Oido allocation-failure patch.
Oido code and weights have separate licenses. See `oido/LICENSE` and
`oido/models/LICENSE-WEIGHTS` before distributing an image or model.

## Install on the tested local-TTS layout

This profile needs a different partition table. It has one application slot.
Create and verify a private full-flash backup before changing the layout. Keep
the matching USB TTS image and its partition table for restoration. These
commands assume the existing local-TTS weights/assets/NVS are already installed.
They are not a full installation onto a factory-layout robot.

| Partition | Address | Size |
|---|---:|---:|
| NVS | `0x9000` | `0x6000` |
| PHY | `0xf000` | `0x1000` |
| OTA metadata | `0x10000` | `0x2000` |
| Application | `0x20000` | `0x3d0000` |
| Oido | `0x3f0000` | `0x5b0000` |
| Ito | `0x9a0000` | `0x310000` |
| Assets | `0xcb0000` | `0x300000` |
| Core dump | `0xfb0000` | `0x10000` |

With the monitor closed, from `firmware/`:

```sh
python -m esptool --chip esp32s3 --port /dev/cu.usbmodem1101 \
  --baud 921600 --after no_reset write_flash \
  0x8000 build-conversation/partition_table/partition-table.bin \
  0x20000 build-conversation/stack-chan.bin \
  0x3f0000 oido/models/oido_v2b.tnm
python -m esptool --chip esp32s3 --port /dev/cu.usbmodem1101 \
  --before no_reset --after watchdog_reset write_mem 0x6000812c 0 1
```

The writes preserve the existing bootloader, OTA metadata, NVS, Ito model and
assets. The model must be exactly 5,931,424 bytes with SHA256
`9e8b40913f532fb44b716a1f882f38b3b21e220713e417899aba32ace8c35066`.
Recognition checks the entire hash before parsing the flash-resident model.
After installation, app-only changes write only `0x20000`. Keep the matching
partition table and both models with recovery artifacts. Use USB recovery for
this profile; it has no second application slot for an OTA rollback.

## Record and speak

Use the ESP-IDF Python, which includes `pyserial`. From `firmware/`:

```sh
python ito/esp32/tools/stackchan.py monitor \
  --port /dev/cu.usbmodem1101 \
  --firmware build-conversation/stack-chan.bin \
  --log-root build-conversation/sessions
```

Wait for `READY`, then type:

```text
volume 50
listen 5
```

Wait for `LISTENING` before speaking. The robot recognizes speech while recording up to five seconds,
then finishes the transcript and restores Ito using its retained weights. To finish early, type
`send`. `cancel` discards an active recording or recognition. Wait for
`CONVERSATION READY` before starting another turn.

After `TRANSCRIPT turn=...`:

```text
echo
text I heard you. This is my manual reply.
status
memory
```

`echo` speaks the latest completed transcript. `text` speaks your supplied
answer. A failed or cancelled turn cannot be echoed. Recognized speech is never
interpreted as a command. Speech can be cancelled with `cancel`.

`listen` without a number records at most ten seconds. `listen N` accepts whole
seconds from 1 to 10. There is no automatic end-of-speech detector. Model setup completes before `LISTENING`. Recognition consumes PCM during
capture; only queued input, the final encoder chunk and Ito restoration remain
after recording ends. Live recognition uses six-frame encoder chunks; uploaded
WAV diagnostics use eight. Greedy decoding uses no optional language model. Check
transcription quality on your own speech before relying on it.

## Fixed audio

To compare recognition with a reproducible input:

```sh
python ito/esp32/tools/stackchan.py transcribe oido/docs/demo/examples/b05.wav \
  --port /dev/cu.usbmodem1101 --echo \
  --firmware build-conversation/stack-chan.bin \
  --asr-weights oido/models/oido_v2b.tnm \
  --log-root build-conversation/sessions
```

Input must be mono PCM16 WAV at 16 kHz, with at most 160,000 samples. Upload
uses acknowledged chunks and does not involve a host recognizer. `--echo`
speaks the transcript after Ito resumes. Close the monitor before this command;
one process owns the port.

## Logs and recovery

The monitor prints its session directory. Logs can contain your speech transcript,
manual replies and uploaded PCM. Keep personal sessions private. Device logs
include capture counts/peak/clipping, recognizer allocation and phase timings,
worker stack watermark, heap sizes and Ito playback/underrun timings. Heap
numbers exclude the reserved inference regions being lent between engines.

An unsuccessful recognition returns to the USB prompt after restoring Ito.
Uncertain clock/power cleanup or a failed ownership handoff closes admission and
requires `reboot` or the physical RST button. It does not attempt playback with
uncertain hardware or memory ownership. Reset returns to USB control with the
microphone off. Networking and a responder can be added after local acceptance.

## Device results

The user confirms microphone recognition, audible Ito echo, mouth animation and
still motors. The retained run contains 91 completed speech turns, twenty public
clips matching the host and twenty microphone handoffs with exact 1-, 3- and
10-second caps. Repeated passes stopped at the user's request. The user also confirms
`listen 5` followed by `echo` from their own monitor session.

[Results and measurements](https://github.com/gianpaj/ito/blob/e3df8e75a0ac115670a547db7f5c35f1084d5bb4/results/stackchan/2026-11-15-manual-oido/README.md) include native memory, stack headroom,
recognition timing, public-set WER, golden PCM, image/model hashes and known
limits. The mixed public WER is 19.63%; it is separate from host/device parity.

## Latency benchmark

The [Ito host benchmark](https://github.com/gianpaj/ito/blob/docs/stackchan-ito-plan/esp32/BENCHMARKING.md#stackchan-manual-speech-latency)
runs from the Ito checkout and uses this firmware's existing USB timing hooks.
It defaults to one recording and a supplied reply, with JSON/CSV results for STT,
restoration and text-to-first-I2S latency. It does not invoke an LLM.

The [streaming measurements](https://github.com/gianpaj/ito/blob/docs/stackchan-ito-plan/results/stackchan/2026-11-15-streaming-latency/README.md)
measure 1.65 seconds from the final RX read to first I2S and 1.11 seconds from
final transcript to first I2S. The strict 1.5-second input-end target remains
open. Fixed-reply benchmarks call no LLM and measure software endpoints rather
than audible onset. Four microphone timing turns and one public-fixture turn
report zero output underruns, including a 7.05-second reply.
