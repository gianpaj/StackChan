# Agent Note: Oido streaming session

Status: implemented — Bounded sessions and native capture handoffs are verified.

## Problem

Batch recognition leaves the complete recognition workload after recording.
Live capture needs an engine lifetime that spans incoming PCM without releasing
Ito's borrowed memory or its process-wide Oido hooks between feeds.

## Decision

`oido_recognizer::Session` owns one stopped-Ito workspace across `begin`,
`feed`, `finish` and `close`. The caller completes `begin` before starting
capture, feeds published PCM on one recognition worker and closes the session
before returning the workspace. Every begin hashes the complete approved model
before parsing it. Weights stay mapped from flash. The session keeps upstream's
single-core weight-preload policy. The dependency patch preserves the ROM
preloader's prior autoload state, and every preload-starting kernel waits before
returning. Cancellation occurs between those calls. Ito's GDMA staging is parked
before the lease begins. Flash writes, MMU/cache reconfiguration and other manual
preload users must remain outside this exclusive session.

One static bounded run holds the model, stream and process-wide Oido hooks.
A second session is rejected. `close` restores the hooks, destroys all model
users and unmaps flash; the destructor also closes. A failed begin cleans up
immediately. Feed/finish failures retain ownership until close and cancellation
remains cooperative. Close returns the retained failure and final metrics.

Feeds consume 1..320 mono 16 kHz samples synchronously, with a cumulative
160,000-sample cap. PCM, transcript storage and the session object stay outside
the workspace. Encoder chunks can be 4, 6 or 8 frames, with context fixed at 64.
The default and batch `recognize` wrapper use 8. Smaller chunks let the
controller measure finish latency without changing decoding or ownership.

The PSRAM admission bound is 1,310,720 bytes and the hot bound is 229,376 bytes.
This admits the 1,695,168-byte tail after Ito's retained weights. Allocations
remain checked against the supplied arena lengths. Setup, accumulated feed
compute and finish times exclude the caller's wait for captured PCM.

## Alternatives considered

Running the batch wrapper after capture adds the complete STT duration to the
response gap. Running inference inside the RX read loop can exceed its DMA
window. The controller uses its existing control task as a separate producer.
Four-frame chunks reduce the final partial chunk but fall behind on the native
five-second probe. Eight-frame chunks keep up but have a longer final chunk.
Unconditionally disabling upstream weight preload discards its build policy.
Keeping that policy with explicit completion and restored autoload avoids a
second asynchronous owner or another task.
Six-frame chunks provide an intermediate configuration; its output is pinned
separately because changing the chunk size can change recognition.

## Consequences

`python3 firmware/tests/oido_recognizer_test.py` passes with ASan and UBSan.
The actual patched engine produces identical 8-frame batch/stream transcripts
for the public b05 fixture. It uses 211,488 hot bytes and 1,309,440 PSRAM bytes on the
host, and passes at the PSRAM admission bound. The 4-frame session produces
the same b05 transcript and uses 188,960 hot bytes and 1,216,624 PSRAM bytes.
The 6-frame session uses 200,224 hot bytes and 1,273,408 PSRAM bytes. Its b05
transcript has one different word, `the spring` rather than the 8-frame
`these spring`. The harness pins each output. This single fixture does not
establish broader recognition quality.
The harness rejects unsupported chunk sizes and checks workspace
aliases, rejected chunks, the sample cap, cancellation after encoder work,
failed setup, reentry and destructor/idempotent close cleanup.

The native five-second 4-frame probe spends 5,606.6 ms in feed compute,
leaves 894 ms of backlog and needs 328.6 ms to finish. Its capture-end to
transcript tail is 1,223.8 ms. The 8-frame probe spends 3,902.6 ms feeding
and 453.9 ms finishing, keeping up with capture. A shorter encoder chunk can
reduce finish work while adding enough feed work to increase total latency.
The six-frame native probe without weight prefetch uses 193,456 hot bytes and
1,273,120 PSRAM bytes. It needs 4,637.3 ms to feed and 312.1 ms to finish, with
245.8 ms of queued input drain. Its final-input-read to transcript tail is
558.8 ms. The controller owns the complete turn measurements.
