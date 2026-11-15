# Agent Note: Bounded microphone capture

Status: implemented — Capture and audible playback recovery pass device acceptance.

## Problem

The local speech profile owns I2S for output and keeps the microphone powered
off. Recording must share the codec without overlapping playback or leaving
uncertain clocks, resources or power after an error.

## Decision

The original `CoreS3AudioCodec` owns both audio directions. Manual capture uses
ES7210 MIC1 at 16 kHz, PCM16 mono, with 30 dB gain. Native 16 kHz capture avoids
a resampler. Continuous listening and a second I2S owner are excluded.

The controller stops Ito before capture and owns the capture worker. The board
API accepts at most 320 samples per read and 160,000 samples per recording.
That limit is ten seconds of stored audio. Startup and scheduling add wall time.
The caller checks cancellation between reads and calls `end_capture()` after
every successful start, including failed reads, early submission and cancel.

## Mechanism

Capture mutes and flushes the speaker, puts AW88298 in standby, disables TX and
releases its pin routing. Standby prevents an operating amplifier from following
the microphone's 16 kHz clocks while configured for 24 kHz playback.
The same codec creates RX on I2S0, powers ALDO2 and initializes ES7210. It discards
three 320-sample startup buffers. Each subsequent driver read has a 40 ms timeout.
ES7210 enable selects microphones and rewrites their gain registers from its
cached defaults. Capture applies 30 dB gain after enable and before unmute.
Capture statistics report delivered samples, peak magnitude, clipped samples
and elapsed time. PCM remains in the controller's buffer.

End disables and deletes ADC/RX resources, verifies microphone power-off and
restores the original 24 kHz TX clock and GPIO routing. The speaker stays muted
while its 16-bit format, amplifier enable and volume are restored. A bounded
200 ms SYSST check requires PLL lock, stable clocks and amplifier switching
before TTS can unmute. Register readbacks cover status, control, mute, format and
volume at initialization, capture handoff and unmute. The TX handle remains the
original codec handle. Failed clock, amplifier, resource or power cleanup blocks output and
unmute until restart. Partial startup attempts run the same cleanup.

The PMIC rail mutex preserves capture power across brightness updates. The rail
defaults off and each change verifies register readback. Motor power remains
clamped off. Touch input, menus and native hardware controls are unchanged.

## Alternatives considered

Capturing at 24 kHz and resampling adds a conversion stage when ES7210 supports
native 16 kHz. The original four-slot TDM path supplies both physical microphones
and the speaker reference; a single physical microphone is sufficient for this
half-duplex manual mode. A second codec owner would compete for the same I2S
controller, so capture stays with the existing owner.

## Consequences

Local and normal ESP-IDF builds pass. Logs are
`firmware/build-local-ito/oido-capture-build.log` and
`firmware/build/oido-capture-normal-build.log`.

`python3 firmware/tests/local_capture_test.py` executes production capture
methods with mocked I2S, ADC and power calls. ASan/UBSan pass for ownership,
read bounds, volume-zero mute and 27 injected startup/cleanup failures, including
amplifier restart and bounded clock-status timeout. ADC and amplifier mocks have
separate state.

Native capture delivers exact 1-, 3- and 10-second sample caps across twenty
recordings, with verified cleanup and no clipping. The human eight-second trial
captures 128,000 samples, peak 7,908 and no clipping. The user confirms an audible
post-capture greeting, followed by local transcription and audible Ito echo with
mouth movement and still motors. These are manual acceptance checks, not a
broad recognition-quality benchmark.

The manufacturer schematic identifies MIC1 and MIC2 as the physical microphones
and MIC3 as speaker feedback. Keep the selected MIC1 route when changing the
capture controller. See the [schematic](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/490/Sch_M5_CoreS3_v1.0.pdf).
Host mocks cannot establish physical signal quality. Private device recordings
and transcripts stay outside Git.
