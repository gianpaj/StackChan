# Agent Note: Local Ito speech in the original application

Status: implemented — USB speech uses the original face and codec.

## Problem

Native speech needs the existing face and audio hardware without cloud speech
services, motor movement or a second I2S owner.

## Decision

Use the original StackChan face and AW88298 codec. The local build boots directly
into the face and takes speech commands over USB. Touch input, the launcher and
setup menus are excluded from the local profile. The normal build keeps its
cloud behavior and controls.

The [touch-free profile note](../simplification/2026-11-15-touch-free-local-profile.md)
records this application boundary and its measurements. Device results below describe
the identified deployed checkpoints.

The Ito component and frontend come from the revision in `firmware/repos.json`.
The shared runtime retains its six-buffer queue, scheduler, calibration,
deterministic self-tests and failed-full split guard. The original codec is the
only I2S owner. It accepts bounded mono writes, drains the actual DMA ring and
flushes all DMA buffers on cancel. Text-job generations reject stale producer
output. The original speaking modifier follows first output and final drain.

The application owns NVS and ESP-IDF's watchdog. A periodic cooperative yield
lets the subscribed IDLE task run during silent calibration. Ito does not take
over RWDT. The hot arena is reserved before HAL initialization and must remain
in internal SRAM. The frontend's static buffers use PSRAM.

## Alternatives considered

Copying a face into the standalone Ito image loses the original application
integration selected by the user. A second codec/I2S owner would make DMA and
cancellation ownership ambiguous.

## Consequences

### Local profile boundary

Motor power is clamped off and motion uses no-op servos. Microphone power stays
off, with PMIC rail readback. The local codec allocates no RX channel or ES7210.
Bluetooth, camera startup, cloud audio/SR services and idle shutdown are disabled.
The local profile excludes app installation and menu entry paths. It creates no
LVGL touch input, Home indicator or status bar. The one-time panel probe and PMIC
power polling remain active. Native hardware power/reset controls are unchanged.

The local profile keeps the standalone Ito NVS layout, `0x9000` with 24 KB.
This preserves the stored failed-full verdict without migration. OTA metadata
starts at `0x10000`; two 4.75 MiB app slots start at `0x20000` and `0x4e0000`.
The model is at `0x9a0000`, assets at `0xcb0000`. Local NVS initialization fails
without erasing safety records. Raw NVS restores require a matching layout.

Registry components have separate normal/local lock files. Both retain all
baseline versions and hashes. Local adds only ESP-NN 1.4.1. The tracked Xiaozhi
patch removes local-only cloud/SR/status-bar references. Its reverse check passes.

## Verification and device checkpoint

The normal and local builds pass. Runtime cancellation/ownership regression tests
pass with actual production C functions and mock queues/codec callbacks.
The standalone StackChan and generic Ito builds, frontend golden tests and
scheduler/split host tests also pass.

Private recovery artifacts live outside Git under the existing device backup.
The full 16 MB factory image was written and verified, without booting it.
The green-dot Ito bootloader, layout, models, app and matching NVS were restored
and verified. It booted, passed both self-tests and played its greeting with zero
underruns. This checks flash recovery, not factory cloud or motor operation.

Device memory, audio, mouth animation and cancellation are verified below.
The selected local-speech scope is implemented and verified.

## First boot and contiguous SRAM

Candidate 1 was written and every image verified. It failed before HAL startup:
239,948 contiguous bytes were required, with 296,439 internal bytes free but a
largest block of 167,936. Its private boot log is retained under
`firmware/build-local-ito/device-tests/sessions/20261114T222616.498049Z/`.
USB reset, default reset and no-reset connection attempts could not reach ROM
while it rebooted. A physical RST download-mode reset was requested.

The hot arena is reserved by an IDF CORE priority-106 initialization hook.
Heap, libc and PSRAM are initialized at that point. C++ constructors and the
DMA pool have not split the largest block. The private startup API is tied to
the pinned IDF 5.5.4. The local DMA reserve is 32 KB, matching the tested native
Ito configuration. Heap and non-ISR ring-buffer code use flash. ISR ring-buffer
functions remain in IRAM. The active local ISR paths do not allocate with the
cache disabled.

The local LCD transfer buffer holds eight rows, preserving the original image.
Unused servo state is excluded, the USB-only console allocates no UART0 driver,
and its 4 KB text buffer uses external BSS. Engine arithmetic and the measured
239,948-byte hot arena are unchanged. Reducing the synthesis arena or moving it
to PSRAM was rejected for this checkpoint.

The corrected build passes. Static DIRAM use is 108,179 bytes, compared with
135,375 bytes in candidate 1. Initialization failures remain in an error/idle
state instead of restarting repeatedly. The corrected candidate is an app-only update because candidate 1's layout,
model and assets are verified.

## Original face and remaining startup memory

Candidate 2 boots after a whole-chip reset. USB-only resets retain a cached low
boot strap in download mode on this unit. Esptool's ESP32-S3 watchdog reset
resamples it; reopening the plain monitor then reaches the app. This is a
controlled reset, not a watchdog failure in the firmware.

The hot arena is confirmed in internal SRAM. The user confirms the original
face is visible and the motors stay still. Before the avatar there are 56,558
internal bytes free; afterward 46,466. Kernel setup leaves 14 KB, insufficient
for all speech task stacks. The app reports the failure and keeps the face live.

The early hook sets ordinary malloc allocations to prefer PSRAM through
`heap_caps_malloc_extmem_enable(0)`. DMA and task stacks still request internal
memory. This keeps UI objects outside the SRAM reserved for synthesis. Head-pet
and IMU modifiers are disabled in this profile, so their sensor initialization
and head-touch worker are omitted. The `f799097` measurement includes display
touch for the original menus. The normal profile retains its original initialization.

The local and normal firmware builds pass. No engine or model arithmetic
changes are part of this correction.

## Deployed validation

The user confirms clear speech, the original mouth animating during speech and
closing afterward, and stationary motors. Candidate 3 uses code `f799097`,
Ito runtime `b741244`, and app SHA256
`a5690f3955fe2f3b0ce360cbd7d0d2bdb31c3eda55dbb30a8146e99be59a0a1e`.
The hot arena is 239,948 bytes in internal SRAM. The failed-full split verdict
and its counters remain intact.

- Boot self-test matches host PCM `363d7d98`, 134,100 samples.
- Quiet numbers match host tokens and PCM `c4472e58`.
- Spoken greeting and numbers have zero underruns. Greeting text arrival to first
  I2S submission is 828.3 ms. This excludes DMA and acoustic latency.
- Cancel flushes in 19.8 ms, closes the mouth and rejects queued old sentences.
  A following numbers request plays successfully.
- The 26-utterance paragraph produces 196 seconds of speech with zero underruns,
  matching frontend tokens and no resets. Aggregate compute RTF is 1.0352.
  The degraded warning remains; the strict RTF-below-1 gate does not pass.
- Warm internal free memory is 13,202 bytes, with a 5,358-byte minimum. The same
  free value remains after the paragraph. PSRAM is about 2.4 MB free after the
  intentional quiet-text scratch ring is allocated.

Evidence is in `firmware/build-local-ito/device-tests/`, including JSON results
and session `20261115T075242.489421Z`. Private image manifests and recovery files
remain outside Git. [Local Ito speech](../../../../firmware/LOCAL_ITO.md) is the
canonical build, monitor and boot guide. Factory cloud/motor operation is outside
this validation. The user confirms Home returns to the launcher and AI.AGENT restores the face.
A second golden self-test and following greeting pass after re-entry, with zero
underruns. Final free internal memory is 13,242 bytes, minimum 5,414 bytes.
The USB monitor is closed, leaving the port available for interactive use.
Three touch-poll timeout reports occurred during initial calibration; the driver
skipped those samples, and the physical Home/re-entry check passed.

## Saved performance and memory results

The [dated measurement report](https://github.com/gianpaj/ito/blob/386fc611f91b07fa3fa57a4f8f0aaeb06e2ed652/results/stackchan/2026-11-15-original-face/README.md) is the canonical detailed record. It includes
per-utterance timings, exact sample-based RTF, memory units and snapshots, test
outcomes, deployment/configuration hashes and a serial measurement trace.
Final static DIRAM is 108,163 bytes in the deployed `f799097` image.

The raw session, metadata, individual test JSON, helpers, host-reference records,
ELF, linker map, SDK configuration and build logs are archived outside the build
tree under `original-ito-candidate-3/validation-artifacts` in the private device
backup. The archive manifest checksums every artifact. Credentials, NVS and full
flash backups remain outside Git.

## USB inference handoff

The local application owns the USB task, so control remains available while Ito
is stopped. `memory` reports allocator bytes. `tts stop` and `tts resume` call
the shared service lifecycle. `tts probe` temporarily borrows the reserved hot
workspace and checks a separate 3 MiB PSRAM allocation. It exercises ownership
without adding STT or changing flash storage.

The [service header](https://github.com/gianpaj/ito/blob/d41d3996eb75f2f3b4d819b073ba35ab448bac92/esp32/runtime/itofs_service.h) defines
resource ownership and failure behavior. The source is pinned in `repos.json`.
The codec remains open, volume persists, and the face stays in standby during
a handoff. Reboot remains available through USB when inference is stopped or
parking fails. Keeping control inside Ito's console was rejected because the
application must own transitions and diagnostics independently of admission.

Integrated device handoff measurements are pending.
