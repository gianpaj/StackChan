import type { AvatarPreset } from "./state";

export const presetLabels: Array<{ preset: AvatarPreset; label: string; key: string }> = [
  { preset: "neutral", label: "Neutral", key: "1" },
  { preset: "happy", label: "Happy", key: "2" },
  { preset: "angry", label: "Angry", key: "3" },
  { preset: "sad", label: "Sad", key: "4" },
  { preset: "doubt", label: "Doubt", key: "5" },
  { preset: "sleepy", label: "Sleepy", key: "6" },
];

