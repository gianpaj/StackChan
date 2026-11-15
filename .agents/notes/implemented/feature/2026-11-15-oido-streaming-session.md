# Oido streaming session

## Decision

`oido_recognizer::Session` owns one stopped-Ito workspace across `begin`,
`feed`, `finish` and `close`. The caller completes `begin` before starting
capture, feeds published PCM on one recognition worker and closes the session
before returning the workspace. Every begin hashes the complete approved model
before parsing it. Weights stay mapped from flash.

One static bounded run holds the model, stream and process-wide Oido hooks.
A second session is rejected. `close` restores the hooks, destroys all model
users and unmaps flash; the destructor also closes. A failed begin cleans up
immediately. Feed/finish failures retain ownership until close and cancellation
remains cooperative. Close returns the retained failure and final metrics.

Feeds consume 1..320 mono 16 kHz samples synchronously, with a cumulative
160,000-sample cap. PCM, transcript storage and the session object stay outside
the workspace. The batch `recognize` wrapper uses the same lifecycle.

The PSRAM admission bound is 1,310,720 bytes and the hot bound is 229,376 bytes.
This admits the 1,695,168-byte tail after Ito's retained weights. Allocations
remain checked against the supplied arena lengths. Setup, accumulated feed
compute and finish times exclude the caller's wait for captured PCM.

## Verification

`python3 firmware/tests/oido_recognizer_test.py` passes with ASan and UBSan.
The actual patched engine produces identical batch/stream transcripts for the
public b05 fixture. It uses 211,488 hot bytes and 1,309,440 PSRAM bytes on the
host, and passes at the PSRAM admission bound. The harness checks workspace
aliases, rejected chunks, the sample cap, cancellation after encoder work,
failed setup, reentry and destructor/idempotent close cleanup.

These checks cover adapter ownership and recognition parity. Streaming native
capture, producer backlog, stack use and turn latency require the integrated
device measurement owned by the conversation controller.
