# Avatar SDL Simulator

## Purpose

Build a host-side macOS simulator executable for the StackChan avatar face. The simulator should open an SDL window through LVGL's SDL backend, render the real firmware avatar skin in a 320x240 viewport, and show controls beside it without flashing a CoreS3.

This first milestone covers only the avatar face. It does not include the launcher, app chrome, Xiaozhi runtime, board drivers, audio, camera, networking, servo control, or ESP-IDF services.

## Goals

- Render `stackchan::avatar::DefaultAvatar` in a macOS SDL window.
- Reuse the firmware avatar implementation instead of rewriting the face in TypeScript or another UI stack.
- Keep the build independent from ESP-IDF.
- Provide controls that mirror the mobile app's avatar controls in `app/lib/view/popup/motion.dart`.
- Establish a small host-side target that can later grow into a broader UI simulator.

## Non-Goals

- Compile the whole firmware for macOS or any other host OS.
- Emulate CoreS3 hardware.
- Run `StackChanAvatarDisplay`.
- Run the launcher or any Mooncake app flow.
- Run Xiaozhi, OpenAI, BLE, Wi-Fi, OTA, camera, audio, RGB LEDs, or servo code.
- Add WebAssembly or browser output in this milestone.

## Proposed Location

```text
firmware/sim/avatar_sdl/
  CMakeLists.txt
  main.cpp
  lv_conf.h
```

The target should live under `firmware/sim/` so it is clearly product simulator code, not a `smooth_ui_toolkit` example or an ESP-IDF component. The first supported development host is macOS.

## Source Reuse

The simulator should compile these avatar files directly:

- `firmware/main/stackchan/avatar/skins/default/default.cpp`
- `firmware/main/stackchan/avatar/skins/default/eyes.cpp`
- `firmware/main/stackchan/avatar/skins/default/mouth.cpp`
- `firmware/main/stackchan/avatar/skins/default/speech_bubble.cpp`
- `firmware/main/stackchan/avatar/skins/default/assets/default_bubble_arrow.c`

It should include the avatar interfaces under:

- `firmware/main/stackchan/avatar/avatar/`
- `firmware/main/stackchan/avatar/skins/default/`

It should also link the host build of:

- `firmware/components/smooth_ui_toolkit`
- LVGL
- SDL2

## Runtime Design

`main.cpp` should own the simulator lifecycle:

1. Initialize LVGL.
2. Create a `320x240` SDL display.
3. Register SDL mouse input as an LVGL pointer input.
4. Create `stackchan::avatar::DefaultAvatar`.
5. Call `avatar.init(lv_screen_active())`.
6. Apply a neutral initial emotion.
7. Run a host-side SDL event loop that handles keyboard input, calls `avatar.update()`, and calls `lv_timer_handler()`.

The simulator should instantiate `DefaultAvatar` directly. It should not instantiate `StackChan` in the first milestone, because that pulls in motion, neon-light, modifier, and hardware-oriented dependencies that are not needed to render the face.

## Keyboard Controls

The first simulator should support direct keyboard controls for fast smoke testing:

```text
1      neutral
2      happy
3      angry
4      sad
5      doubtful
6      sleepy
space  toggle mouth open/closed
b      blink once
s      show/hide speech bubble
esc    quit
```

These controls should call avatar APIs directly:

- `avatar.setEmotion(...)`
- `avatar.mouth().setWeight(...)`
- `avatar.leftEye().setWeight(...)`
- `avatar.rightEye().setWeight(...)`
- `avatar.setSpeech(...)`
- `avatar.clearSpeech()`

The first version should not use `BlinkModifier`, `BreathModifier`, or `SpeakingModifier`, because those include `hal/hal.h` and depend on `GetHAL().millis()` or motion APIs.

## Avatar Control Model

The simulator should use the same avatar control model as the mobile app's Motion popup. In `app/lib/view/popup/motion.dart`, the Avatar tab exposes sliders for:

```text
leftEye:  x, y, rotation, weight, size
rightEye: x, y, rotation, weight, size
mouth:    x, y, rotation, weight
```

Those fields map directly to the firmware avatar feature API:

```text
x, y      -> Feature::setPosition({x, y})
rotation  -> Feature::setRotation(rotation)
weight    -> Feature::setWeight(weight)
size      -> Feature::setSize(size)
```

The simulator should represent this as a local `ExpressionState` struct with the same fields as the Flutter `ExpressionData` model:

```json
{
  "type": "bleAvatar",
  "leftEye":  { "x": 0, "y": 0, "rotation": 0, "weight": 100, "size": 0 },
  "rightEye": { "x": 0, "y": 0, "rotation": 0, "weight": 100, "size": 0 },
  "mouth":    { "x": 0, "y": 0, "rotation": 0, "weight": 0,   "size": 0 }
}
```

When a control changes, the simulator should update `ExpressionState` and apply the changed values directly to the avatar. This keeps the first milestone independent from ArduinoJson and the full `stackchan/json/json_helper.cpp` dependency chain. A later milestone can add JSON import/export using the same shape.

## Interactive Control Surface

Keyboard shortcuts are enough to prove that rendering works, but they are not enough to emulate the mobile app's control workflow. After the SDL window renders the face, the first useful control surface should be an SDL-side debug panel or a simple ImGui panel beside the 320x240 avatar viewport.

The panel should be controllable with both mouse and keyboard:

- Mouse: drag sliders, click reset, click presets, and toggle speech.
- Keyboard: switch focused control, nudge values, trigger presets, reset state, and toggle speech.

The panel should expose the same sliders as the mobile app:

```text
Left Eye
  x         -100..100
  y         -100..100
  rotation  -1800..1800
  weight    0..100
  size      -100..100

Right Eye
  x         -100..100
  y         -100..100
  rotation  -1800..1800
  weight    0..100
  size      -100..100

Mouth
  x         -100..100
  y         -100..100
  rotation  -1800..1800
  weight    0..100
```

The panel should also include:

- Reset Avatar: restore left and right eye weight to `100`, mouth weight to `0`, and all position, rotation, and size fields to `0`.
- Speech: toggle a sample speech bubble string.
- Presets: apply the same emotion presets exposed through the keyboard controls.

Keyboard navigation should support testing without touching the mouse:

```text
tab / shift-tab  move focus between controls
left / right     decrement or increment focused slider
up / down        larger decrement or increment focused slider
r                reset avatar
s                toggle speech bubble
1..6             emotion presets
esc              quit
```

Mouse input should stay local to the simulator UI. It should not be interpreted as firmware touch input in this first milestone, because the avatar face has no touch behavior to test.

Motion controls from the mobile app's Motion tab should stay out of the first milestone. Those controls drive `yawServo` and `pitchServo`; an avatar-face-only simulator has no rendered body or virtual head pose yet. A later simulator mode can add a virtual head transform or a body preview and then reuse the mobile ranges:

```text
yawServo.angle    -1280..1280
yawServo.speed    0..1000
yawServo.rotate   -1000..1000
pitchServo.angle  0..900
pitchServo.speed  0..1000
```

## Build Interface

The intended local workflow is:

```bash
cd firmware/sim/avatar_sdl
cmake -S . -B build
cmake --build build
./build/avatar_sdl
```

If SDL2 is missing, CMake should fail with a clear dependency error from `find_package(SDL2)`.

## Expected Result

A successful first milestone opens a macOS SDL window that shows the default StackChan face on a black 320x240 panel. The eyes, mouth, emotion state, and speech bubble should respond to keyboard controls. The next acceptance step is an interactive slider panel that mirrors the mobile app's Avatar tab.

## Implementation Notes

- Use LVGL's SDL backend, following the existing pattern in `firmware/components/smooth_ui_toolkit/example/utils/lvgl_wrapper.hpp`.
- Keep the simulator loop single-threaded.
- Avoid ESP-IDF headers in the simulator target.
- Prefer direct avatar calls over firmware app shell integration.
- Keep placeholder or simulated behavior local to `firmware/sim/avatar_sdl`.
- Treat the mobile app's `ExpressionData` shape as the canonical simulator control state for avatar-only controls.

## Future Work

After the avatar-only simulator works, the next useful increments are:

- Add a tiny simulator clock and enable blink or breath animation without pulling in the full HAL.
- Add JSON import/export for `ExpressionData` so values can be copied between the simulator, mobile app, and firmware tests.
- Add optional decorator previews after image assets compile cleanly on host.
- Add screenshot-based smoke tests.
- Add launcher and setup screens as separate simulator modes.
- Add motion controls once there is a rendered head/body transform to control.
- Add an Emscripten build once the native SDL simulator is stable.
