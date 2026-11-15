# Agent Note: Manual local speech through USB

Status: proposed — Integration and device acceptance are in progress.

## Problem

The USB speech profile accepts typed text but cannot recognize a spoken question.
Both speech models must fit on the robot before adding a Linux responder.

## Proposal

A separate conversation build preserves the original face and uses USB to own
one half-duplex turn. `listen [1..10]` starts bounded microphone capture; `send`
submits early; `cancel` discards it. Oido v2b uses flash weights and greedy
streaming decoding without the optional language model. The transcript is
printed after recognition and Ito restoration. `echo` speaks that transcript;
`text <reply>` accepts a manual response. Recognized text is data, never a
console command. A bounded PCM16LE upload exercises fixed WAV fixtures without
needing someone at the microphone.

Ito parks before capture and lends both reserved inference regions. The
recognizer destroys every model/stream user before returning the lease. A
12 KiB internal-stack worker keeps USB cancellation available and has its own
watchdog subscription. Its stack and the 8 KiB USB control stack are reserved
after the hot arena during early startup. Late stack allocations can fail
even with enough total internal free memory. Buffered USB RX avoids polling
one hardware FIFO every 20 ms while uploading fixed audio. An uncertain audio clock/power cleanup fails closed and
requires reboot. Microphone power is enabled only during capture; motor power
stays clamped off. Physical buttons keep their existing behavior.

The conversation layout retains the app address, NVS, Ito weights and assets.
It replaces the second OTA slot with Oido storage and leaves one application
slot. The tested USB profile and a fresh private full-flash snapshot are the
recovery artifacts. This diagnostic profile has no OTA-update promise.

## Alternatives considered

Keeping two current OTA slots cannot fit the 5,931,424-byte Oido model alongside
Ito and assets. Shrinking/removing the face conflicts with the required display.
Keeping both inference engines active wastes their shared phase budget. A host
recognizer would not prove local STT. Always-listening and automatic responses
add behavior that the manual integration test does not require.

## Acceptance criteria

Build the normal, USB TTS and conversation profiles. Verify exact flash bounds,
backup hashes and recovery. Recognize pinned public PCM on the device, compare
host output, then speak a transcript/manual reply with zero underruns. Test
silence, invalid upload, cancellation, capture caps, repeat turns and post-Oido
Ito golden PCM. Record allocation/timing/stack evidence and unresolved limits.
Private human audio and transcripts remain outside Git.

## Risks

Quad-PSRAM/flash timings may differ from upstream octal hardware. No-LM accuracy
is a measured limitation, not a promise. The upstream allocation paths require
small failure checks before using bounded borrowed arenas. A single app slot
requires USB recovery after a bad flash. Real microphone quality needs a user
speaking beside the robot.
