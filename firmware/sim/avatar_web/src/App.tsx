import { useCallback, useMemo, useState } from "react";
import { AvatarCanvas } from "./AvatarCanvas";
import { AvatarControls } from "./controls/AvatarControls";
import {
  applyPreset,
  createNeutralAvatarState,
  flatSliders,
  getSliderValue,
  updateSliderValue,
  type SliderId,
} from "./avatar/state";
import { useAvatarKeyboard } from "./useAvatarKeyboard";

export function App() {
  const [avatarState, setAvatarState] = useState(createNeutralAvatarState);
  const [selectedSlider, setSelectedSlider] = useState<SliderId>(flatSliders[0].id);

  const selectedIndex = useMemo(
    () => flatSliders.findIndex((slider) => slider.id === selectedSlider),
    [selectedSlider],
  );

  const nudgeSelected = useCallback(
    (delta: number) => {
      const slider = flatSliders[selectedIndex] ?? flatSliders[0];
      setAvatarState((current) =>
        updateSliderValue(current, slider, getSliderValue(current, slider) + delta),
      );
    },
    [selectedIndex],
  );

  const cycleSelected = useCallback(
    (direction: number) => {
      const nextIndex = (selectedIndex + direction + flatSliders.length) % flatSliders.length;
      setSelectedSlider(flatSliders[nextIndex].id);
    },
    [selectedIndex],
  );

  useAvatarKeyboard({
    onPreset: (preset) => setAvatarState((current) => applyPreset(current, preset)),
    onReset: () => setAvatarState(createNeutralAvatarState()),
    onSpeech: () => setAvatarState((current) => ({ ...current, speechVisible: !current.speechVisible })),
    onBlink: () => setAvatarState((current) => ({ ...current, blinkClosed: !current.blinkClosed })),
    onCycle: cycleSelected,
    onNudge: nudgeSelected,
  });

  return (
    <main className="app-shell">
      <section className="sim-stage">
        <div className="stage-header">
          <div>
            <p className="eyebrow">StackChan web simulator</p>
            <h1>Avatar face</h1>
          </div>
          <div className="screen-badge">320 × 240</div>
        </div>
        <AvatarCanvas state={avatarState} />
        <div className="keyboard-map" aria-label="Keyboard shortcuts">
          <span>1 Neutral</span>
          <span>2 Happy</span>
          <span>3 Angry</span>
          <span>4 Sad</span>
          <span>5 Doubt</span>
          <span>6 Sleepy</span>
          <span>R Reset</span>
          <span>S Speech</span>
          <span>B Blink</span>
        </div>
      </section>

      <AvatarControls
        state={avatarState}
        selectedSlider={selectedSlider}
        onChange={setAvatarState}
        onSelectSlider={setSelectedSlider}
      />
    </main>
  );
}
