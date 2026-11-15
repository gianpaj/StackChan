# Agent Note: Manual local speech through USB

Status: implemented — Native microphone recognition and Ito playback pass user acceptance.

## Problem

The USB speech profile accepts typed text but cannot recognize a spoken question.
Both speech models must fit on the robot before adding a Linux responder.

## Decision

A separate conversation build preserves the original face and uses USB to own
one half-duplex turn. `listen [1..10]` starts bounded microphone capture; `send`
submits early; `cancel` discards it. Oido v2b uses flash weights and greedy
streaming decoding without the optional language model. The USB control task captures PCM while the recognition worker feeds the
streaming API with six encoder frames per chunk and 64 frames of context.
Uploaded batch diagnostics retain eight-frame chunks. Model setup finishes
before the LISTENING cue. The model stays flash-backed;
mandatory transposed depthwise weights count in its measured workspace. The
transcript is printed after recognition and Ito restoration. `echo` speaks that transcript;
`text <reply>` accepts a manual response. Recognized text is data, never a
console command. A bounded PCM16LE upload exercises fixed WAV fixtures without
needing someone at the microphone.

Ito parks before capture and lends its hot arena and the PSRAM scratch tail,
preserving its copied weights. The
recognizer destroys every model/stream user before returning the lease. A
12 KiB internal-stack worker keeps USB cancellation available and has its own
watchdog subscription. Its stack and the 8 KiB USB control stack are reserved
after the hot arena during early startup. The existing USB task produces into
the bounded PCM buffer and publishes complete reads atomically. It runs above
recognition priority and polls USB without waiting between active RX reads.
The recognition worker joins producer cleanup before closing Oido and returning
the lease. This adds no task stack. Late stack allocations can fail
even with enough total internal free memory. Buffered USB RX avoids polling
one hardware FIFO every 20 ms while uploading fixed audio. Uncertain audio
clock/power cleanup fails closed and requires reboot. Microphone power is enabled only during capture; motor power
stays clamped off. Physical buttons keep their existing behavior.

The conversation layout retains the app address, NVS, Ito weights and assets.
It replaces the second OTA slot with Oido storage and leaves one application
slot. The tested USB profile and a fresh private full-flash snapshot are the
recovery artifacts. This diagnostic profile has no OTA-update promise.

## Alternatives considered

Keeping two current OTA slots cannot fit the 5,931,424-byte Oido model alongside
Ito and assets. Shrinking/removing the face conflicts with the required display.
Keeping both inference engines active wastes their shared phase budget.
A 16 KiB recognizer stack consumes DMA memory needed by capture; the measured
12 KiB stack retains 5,760 unused bytes. Late task-stack allocations can fail
through fragmentation, so both stacks are reserved during early startup.
A separate capture task would consume scarce internal SRAM. Feeding recognition
in the RX read loop risks losing audio while an encoder chunk runs. The existing
control task can produce PCM without either cost.
A host recognizer would not prove local STT. Always-listening and automatic responses
add behavior that the manual integration test does not require.

## Consequences

The normal, local TTS and conversation builds pass. The robot completes 91
recorded STT/TTS turns, including twenty public clips whose output matches the
host, and twenty microphone handoffs with exact 1-, 3- and 10-second caps.
Repeated passes stop at the user's request. The 100-turn target is not claimed.
Nine explicit Ito test commands pass 27 golden cases after shared-memory reuse;
the boot self-test also passes. Recorded playback reports zero underruns.

The user confirms an audible post-capture greeting and microphone-to-transcript
-to-Ito echo with mouth animation and still motors. The model misrecognizes a
proper name in this pilot. Greedy quality on the mixed public set is a separate
measurement from host/device agreement. The batch baseline records before recognition. Streaming latency is measured
separately from that accepted baseline.

The user independently confirms `listen 5`, then `echo` after
`CONVERSATION READY`, from their own terminal. The supplied log verifies the
five-second cap, successful cleanup and zero output underruns. This confirmation
is separate from the automated run; no additional agent device pass is needed.

Oido uses 204,720 bytes of the borrowed hot region and 1,309,152 bytes of the
borrowed PSRAM pool. The 320,000-byte recording buffer is separate. Ordinary
free heap excludes the two reserved inference regions. A dedicated recognizer
watchdog is fed only by its owner. CPU utilization and RX overflow counters are
not instrumented. TURN_TIMING records completion of the last RX read, drained
input, final recognition and restoration on the device monotonic clock. The last
read timestamp is a software endpoint, not detected acoustic speech end. Further native cancellation, battery/reset and full recovery
rehearsals remain deferred; host tests cover the implemented failure paths.

The [command guide](../../../../firmware/LOCAL_CONVERSATION.md) describes the
manual interface and single-slot recovery limits. The
[capture decision](../hardware/2026-11-15-bounded-microphone-capture.md)
defines codec/power ownership and the required ADC gain and amplifier restart
sequences. Model weights, private human transcripts and full flash backups stay
outside Git. No network responder is installed by this milestone.

[Device measurements](https://github.com/gianpaj/ito/blob/e3df8e75a0ac115670a547db7f5c35f1084d5bb4/results/stackchan/2026-11-15-manual-oido/README.md) retain the exact run scope and provenance.
