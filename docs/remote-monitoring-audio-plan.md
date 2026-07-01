# StackChan Remote Monitoring Audio Plan

## Goal

Extend the existing monitoring mode so the mobile app can:

- watch the live camera feed
- keep joystick motion control
- listen to the StackChan microphone in near real time
- hold-to-talk or toggle-talk and send phone microphone audio back to StackChan's speaker

## What Already Exists

### App

- `app/lib/view/home/monitoring_camera.dart` already subscribes to JPEG frames and already decodes incoming `Opus` packets for playback.
- The same screen contains commented-out `OnAudio` / `OffAudio` subscription calls and a commented-out uplink path that prepends the device MAC and sends `MsgType.opus`.
- `app/lib/util/audio_engine_manager.dart` already initializes Opus at `16 kHz`, mono, `320` samples per frame, and can both decode incoming Opus and encode microphone PCM into Opus frames.
- Android already exposes microphone capture and PCM playback through `MainActivity.kt`.
- iOS playback exists, but microphone capture / record event streaming is not implemented yet.

### Server

- `server/internal/web_socket/web_socket.go` already defines `Opus`, `OnAudio`, and `OffAudio`.
- The server already maintains audio subscribers per StackChan device.
- StackChan -> app audio relay is already implemented for incoming `Opus` frames.
- App -> StackChan uplink is already implemented for `Opus` when the app payload is `[mac(12 bytes)] + opus frame`.

### Firmware

- `firmware/main/hal/hal_ws_avatar.cpp` already defines `StartAudioStream` / `StopAudioStream`.
- Camera streaming is fully wired here.
- Audio handling is stubbed: `StartAudioStream` and `StopAudioStream` do nothing, and the fast-path decode for incoming `Opus` is commented out.
- `firmware/main/hal/audio.cpp` and `firmware/main/hal/board/cores3_audio_codec.*` already prove the device can read microphone PCM and write speaker PCM through the codec.

## Short Verdict

This is mostly an unfinished feature, not a greenfield one.

The transport and message types are already in place. The main missing work is:

1. finish firmware audio streaming and playback
2. re-enable app monitoring audio controls
3. implement iOS microphone capture if two-way talk is required on iPhone

## Recommended Architecture

Use the existing WebSocket binary protocol and keep the current message format:

- device -> server -> app:
  - `0x18 OnAudio` to subscribe
  - `0x01 Opus` for audio frames
  - `0x19 OffAudio` to unsubscribe
- app -> server -> device:
  - send `0x01 Opus` with payload `[mac][opus]`

### Downlink: StackChan Mic to App

1. App enters monitoring screen.
2. App sends `OnCamera` and `OnAudio`.
3. Firmware enables mic capture when the first `OnAudio` arrives.
4. Firmware reads PCM frames from the codec, encodes to Opus, and sends `MsgType::Opus`.
5. App receives `Opus` and plays it immediately through `AudioEngineManager.playOpus(...)`.

### Uplink: App Mic to StackChan Speaker

1. User presses and holds a talk button or enables push-to-talk.
2. App starts native microphone capture.
3. `AudioEngineManager` encodes frames to Opus.
4. App prepends the 12-byte MAC and sends `MsgType.opus`.
5. Server forwards to the target StackChan.
6. Firmware decodes Opus and pushes PCM to `audio_codec->OutputData(...)`.

## UI Recommendation

Keep the current monitoring screen and add only two audio controls:

- speaker/listen toggle
- push-to-talk button

Optional:

- mic level meter while talking
- small "LIVE AUDIO" indicator when subscribed
- automatic speaker mute while push-to-talk is active to reduce acoustic feedback

Push-to-talk is safer than full duplex for V1 because it reduces echo, avoids feedback loops, and keeps CPU/network load lower.

## Firmware Work Plan

### 1. Add an audio stream worker

Inside `hal_ws_avatar.cpp`, add:

- `_is_audio_streaming`
- `_audio_uplink_enabled`
- a small task or loop branch that:
  - enables codec input
  - reads PCM in fixed 20 ms frames
  - encodes to Opus
  - sends `DataType::Opus`

Recommended frame shape:

- sample rate: `16000`
- channels: `1`
- frame size: `320` samples

This matches the Flutter app's existing Opus settings.

### 2. Implement subscription control

In the existing message switch:

- `StartAudioStream` should set `_is_audio_streaming = true`
- `StopAudioStream` should set `_is_audio_streaming = false`
- disable codec input when no longer needed

### 3. Implement incoming Opus decode

Re-enable the fast path for binary `Opus` packets. Decode incoming Opus and write PCM to the speaker.

Important:

- queue incoming packets instead of decoding inline on the websocket callback path
- cap queue depth so stale talkback frames are dropped instead of building latency

### 4. Add echo-control guardrails

For V1:

- prefer half duplex
- while playing remote talkback audio, either:
  - pause mic uplink, or
  - reduce mic gain / ignore uplink

If full duplex is required later, use the existing audio reference path and AEC-related configuration as a V2 task.

## App Work Plan

### 1. Re-enable monitoring audio

In `monitoring_camera.dart`:

- send `OnAudio` on enter
- send `OffAudio` on dispose
- restore `AudioEngineManager.shared.onAudioData`
- restore recorder start/stop logic

### 2. Add audio controls

Add:

- a listen toggle that only subscribes/unsubscribes
- a push-to-talk button for uplink

Recommended behavior:

- entering the page starts camera only
- listen is opt-in
- talk starts recording only while pressed

### 3. Finish iOS capture support

Android is close to ready because `EventChannel("com.m5stack.stackchan/record")` already exists.

iOS still needs:

- `startRecording`
- `stopRecording`
- a record event stream equivalent to Android
- microphone permission flow

Without that, two-way audio works on Android first and remains listen-only on iOS.

## Server Changes

Server changes should be minimal.

Recommended cleanup only:

- fix the disconnect path that appears to send `OnAudio` when the last subscriber is removed; that should be `OffAudio`
- add structured logs for audio subscribe/unsubscribe counts
- optionally reject app uplink `Opus` when the target device is offline

## Rollout Order

1. Firmware: implement `OnAudio` mic streaming only
2. App: re-enable listen-only monitoring mode
3. Verify latency, packet loss, and battery impact
4. Firmware: add incoming Opus playback
5. App Android: enable push-to-talk
6. App iOS: add native recording support
7. Optional V2: full duplex, AEC, adaptive bitrate

## Acceptance Criteria

### V1 Listen-only

- user opens monitoring page
- camera feed appears
- user enables listen
- app receives continuous Opus audio from StackChan mic
- leaving page stops both camera and audio subscription

### V1 Push-to-talk

- user presses talk button
- app sends live Opus uplink
- StackChan speaker plays audio with sub-second latency
- releasing talk stops uplink within one second

## Main Risks

- iOS uplink is incomplete today
- full duplex will likely echo badly without AEC tuning
- ESP32 CPU and Wi-Fi bandwidth may need tighter frame pacing when camera and audio run together
- stale audio queues will feel broken faster than dropped packets

## Best First Implementation

The fastest useful version is:

- camera + joystick + listen-only audio on both platforms
- Android push-to-talk
- iOS talkback deferred

That path reuses the most existing code and avoids blocking the entire feature on iPhone native recording support.
