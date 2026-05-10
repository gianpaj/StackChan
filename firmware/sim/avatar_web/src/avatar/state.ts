export type AvatarFeature = {
  x: number;
  y: number;
  rotation: number;
  weight: number;
  size: number;
};

export type MouthFeature = Omit<AvatarFeature, "size">;

export type AvatarState = {
  leftEye: AvatarFeature;
  rightEye: AvatarFeature;
  mouth: MouthFeature;
  speechVisible: boolean;
  blinkClosed: boolean;
  preset: AvatarPreset;
};

export type AvatarPreset =
  | "neutral"
  | "happy"
  | "angry"
  | "sad"
  | "doubt"
  | "sleepy";

export type FeatureTarget = "leftEye" | "rightEye" | "mouth";

export type SliderId =
  | "leftEye.x"
  | "leftEye.y"
  | "leftEye.rotation"
  | "leftEye.weight"
  | "leftEye.size"
  | "rightEye.x"
  | "rightEye.y"
  | "rightEye.rotation"
  | "rightEye.weight"
  | "rightEye.size"
  | "mouth.x"
  | "mouth.y"
  | "mouth.rotation"
  | "mouth.weight";

export type SliderDefinition = {
  id: SliderId;
  target: FeatureTarget;
  field: keyof AvatarFeature;
  label: string;
  min: number;
  max: number;
};

export const sliderSections: Array<{
  title: string;
  sliders: SliderDefinition[];
}> = [
  {
    title: "Left Eye",
    sliders: [
      { id: "leftEye.x", target: "leftEye", field: "x", label: "x", min: -100, max: 100 },
      { id: "leftEye.y", target: "leftEye", field: "y", label: "y", min: -100, max: 100 },
      {
        id: "leftEye.rotation",
        target: "leftEye",
        field: "rotation",
        label: "rotation",
        min: -1800,
        max: 1800,
      },
      { id: "leftEye.weight", target: "leftEye", field: "weight", label: "weight", min: 0, max: 100 },
      { id: "leftEye.size", target: "leftEye", field: "size", label: "size", min: -100, max: 100 },
    ],
  },
  {
    title: "Right Eye",
    sliders: [
      { id: "rightEye.x", target: "rightEye", field: "x", label: "x", min: -100, max: 100 },
      { id: "rightEye.y", target: "rightEye", field: "y", label: "y", min: -100, max: 100 },
      {
        id: "rightEye.rotation",
        target: "rightEye",
        field: "rotation",
        label: "rotation",
        min: -1800,
        max: 1800,
      },
      {
        id: "rightEye.weight",
        target: "rightEye",
        field: "weight",
        label: "weight",
        min: 0,
        max: 100,
      },
      { id: "rightEye.size", target: "rightEye", field: "size", label: "size", min: -100, max: 100 },
    ],
  },
  {
    title: "Mouth",
    sliders: [
      { id: "mouth.x", target: "mouth", field: "x", label: "x", min: -100, max: 100 },
      { id: "mouth.y", target: "mouth", field: "y", label: "y", min: -100, max: 100 },
      {
        id: "mouth.rotation",
        target: "mouth",
        field: "rotation",
        label: "rotation",
        min: -1800,
        max: 1800,
      },
      { id: "mouth.weight", target: "mouth", field: "weight", label: "weight", min: 0, max: 100 },
    ],
  },
];

export const flatSliders = sliderSections.flatMap((section) => section.sliders);

export const createNeutralAvatarState = (): AvatarState => ({
  leftEye: { x: 0, y: 0, rotation: 0, weight: 100, size: 0 },
  rightEye: { x: 0, y: 0, rotation: 0, weight: 100, size: 0 },
  mouth: { x: 0, y: 0, rotation: 0, weight: 0 },
  speechVisible: false,
  blinkClosed: false,
  preset: "neutral",
});

export function applyPreset(state: AvatarState, preset: AvatarPreset): AvatarState {
  const next = {
    ...createNeutralAvatarState(),
    speechVisible: state.speechVisible,
    preset,
  };

  switch (preset) {
    case "happy":
      next.leftEye.weight = 72;
      next.rightEye.weight = 72;
      next.leftEye.rotation = 1550;
      next.rightEye.rotation = -1550;
      break;
    case "angry":
      next.leftEye.weight = 70;
      next.rightEye.weight = 70;
      next.leftEye.rotation = 450;
      next.rightEye.rotation = -450;
      break;
    case "sad":
      next.leftEye.weight = 70;
      next.rightEye.weight = 70;
      next.leftEye.rotation = -400;
      next.rightEye.rotation = 400;
      break;
    case "doubt":
      next.leftEye.weight = 75;
      next.rightEye.weight = 75;
      break;
    case "sleepy":
      next.leftEye.weight = 35;
      next.rightEye.weight = 35;
      next.leftEye.rotation = -50;
      next.rightEye.rotation = 50;
      break;
    case "neutral":
      break;
  }

  return next;
}

export function updateSliderValue(
  state: AvatarState,
  slider: SliderDefinition,
  value: number,
): AvatarState {
  const clamped = Math.max(slider.min, Math.min(slider.max, Math.round(value)));
  return {
    ...state,
    blinkClosed: false,
    preset: "neutral",
    [slider.target]: {
      ...state[slider.target],
      [slider.field]: clamped,
    },
  };
}

export function getSliderValue(state: AvatarState, slider: SliderDefinition): number {
  return Number((state[slider.target] as Record<string, number>)[slider.field]);
}
