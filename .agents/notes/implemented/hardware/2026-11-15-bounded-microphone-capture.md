# Bounded microphone capture

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

Capture mutes and flushes the speaker, disables TX and releases its pin routing.
The same codec creates RX on I2S0, powers ALDO2 and initializes ES7210. It discards
three 320-sample startup buffers. Each subsequent driver read has a 40 ms timeout.
ES7210 enable selects microphones and rewrites their gain registers from its
cached defaults. Capture applies 30 dB gain after enable and before unmute.
Capture statistics report delivered samples, peak magnitude, clipped samples
and elapsed time. PCM remains in the controller's buffer.

End disables and deletes ADC/RX resources, verifies microphone power-off and
restores the original 24 kHz TX clock and GPIO routing. The speaker stays muted
until TTS playback. Failed clock, resource or power cleanup blocks output and
unmute until restart. Partial startup attempts run the same cleanup.

The PMIC rail mutex preserves capture power across brightness updates. The rail
defaults off and each change verifies register readback. Motor power remains
clamped off. Touch input, menus and native hardware controls are unchanged.

## Verification and limits

Local and normal ESP-IDF builds pass. Logs are
`firmware/build-local-ito/oido-capture-build.log` and
`firmware/build/oido-capture-normal-build.log`.

`python3 firmware/tests/local_capture_test.py` executes production capture
methods with mocked I2S, ADC and power calls. ASan/UBSan pass for ownership,
read bounds, volume-zero mute and 21 injected startup/cleanup failures.

Physical MIC1 routing, capture levels, clipping, power-off readback, recognition
accuracy and TTS recovery after real capture remain device validation gates.
Host mocks do not establish those properties. This implementation step performs
no device or serial access.
