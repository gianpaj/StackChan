# Local Ito speech in the original application

## Decision

Use the original StackChan face, launcher, setup controls and AW88298 codec.
A dedicated local build replaces the cloud AI handoff. The normal build keeps
its cloud behavior. Copying a face into the standalone Ito image was rejected
because the user chose full original-firmware integration.

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

## Local profile boundary

Motor power is clamped off and motion uses no-op servos. Microphone power stays
off, with PMIC rail readback. The local codec allocates no RX channel or ES7210.
Bluetooth, camera startup, cloud audio/SR services and idle shutdown are disabled.
Unsupported launcher/setup actions keep their labels and show an existing toast.
Brightness, volume and the local avatar remain usable without network setup.

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

Integrated device memory, audio, mouth animation, cancel and re-entry validation
remain the next checkpoint. Keep this note proposed until those results exist.
