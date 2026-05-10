import { Eye, MessageSquareText, RotateCcw } from "lucide-react";
import { presetLabels } from "../avatar/presets";
import {
  applyPreset,
  createNeutralAvatarState,
  flatSliders,
  getSliderValue,
  sliderSections,
  updateSliderValue,
  type AvatarPreset,
  type AvatarState,
  type SliderDefinition,
  type SliderId,
} from "../avatar/state";

type AvatarControlsProps = {
  state: AvatarState;
  selectedSlider: SliderId;
  onChange: (state: AvatarState) => void;
  onSelectSlider: (id: SliderId) => void;
};

export function AvatarControls({
  state,
  selectedSlider,
  onChange,
  onSelectSlider,
}: AvatarControlsProps) {
  const setSlider = (slider: SliderDefinition, value: number) => {
    onSelectSlider(slider.id);
    onChange(updateSliderValue(state, slider, value));
  };

  const setPreset = (preset: AvatarPreset) => {
    onChange(applyPreset(state, preset));
  };

  return (
    <aside className="control-panel" aria-label="Avatar controls">
      <div className="tool-row">
        <button className="tool-button" type="button" onClick={() => onChange(createNeutralAvatarState())}>
          <RotateCcw size={16} />
          <span>Reset</span>
        </button>
        <button
          className={`tool-button ${state.speechVisible ? "is-active" : ""}`}
          type="button"
          onClick={() => onChange({ ...state, speechVisible: !state.speechVisible })}
        >
          <MessageSquareText size={16} />
          <span>Speech</span>
        </button>
        <button
          className={`tool-button ${state.blinkClosed ? "is-active" : ""}`}
          type="button"
          onClick={() => onChange({ ...state, blinkClosed: !state.blinkClosed })}
        >
          <Eye size={16} />
          <span>Blink</span>
        </button>
      </div>

      <div className="preset-grid" aria-label="Emotion presets">
        {presetLabels.map(({ preset, label, key }) => (
          <button
            key={preset}
            className={`preset-button ${state.preset === preset ? "is-active" : ""}`}
            type="button"
            onClick={() => setPreset(preset)}
            title={`${label} (${key})`}
          >
            <span>{key}</span>
            {label}
          </button>
        ))}
      </div>

      <div className="slider-groups">
        {sliderSections.map((section) => (
          <section className="slider-section" key={section.title}>
            <h2>{section.title}</h2>
            {section.sliders.map((slider) => (
              <ControlSlider
                key={slider.id}
                slider={slider}
                selected={slider.id === selectedSlider}
                value={getSliderValue(state, slider)}
                onChange={(value) => setSlider(slider, value)}
                onFocus={() => onSelectSlider(slider.id)}
              />
            ))}
          </section>
        ))}
      </div>

      <div className="status-strip">
        <span>{flatSliders.find((slider) => slider.id === selectedSlider)?.id}</span>
        <span>tab focus · arrows nudge · 1-6 presets</span>
      </div>
    </aside>
  );
}

type ControlSliderProps = {
  slider: SliderDefinition;
  selected: boolean;
  value: number;
  onChange: (value: number) => void;
  onFocus: () => void;
};

function ControlSlider({ slider, selected, value, onChange, onFocus }: ControlSliderProps) {
  return (
    <label className={`control-slider ${selected ? "is-selected" : ""}`}>
      <span className="slider-label">{slider.label}</span>
      <input
        type="range"
        min={slider.min}
        max={slider.max}
        value={value}
        onChange={(event) => onChange(Number(event.currentTarget.value))}
        onFocus={onFocus}
      />
      <output>{value}</output>
    </label>
  );
}
