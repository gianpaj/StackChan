import { useEffect } from "react";
import type { AvatarPreset } from "./avatar/state";

type KeyboardHandlers = {
  onPreset: (preset: AvatarPreset) => void;
  onReset: () => void;
  onSpeech: () => void;
  onBlink: () => void;
  onCycle: (direction: number) => void;
  onNudge: (delta: number) => void;
};

const presetByKey: Record<string, AvatarPreset> = {
  "1": "neutral",
  "2": "happy",
  "3": "angry",
  "4": "sad",
  "5": "doubt",
  "6": "sleepy",
};

export function useAvatarKeyboard(handlers: KeyboardHandlers) {
  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      const target = event.target as HTMLElement | null;
      const isTextInput = target?.tagName === "INPUT" && (target as HTMLInputElement).type !== "range";
      if (isTextInput) {
        return;
      }

      if (event.key in presetByKey) {
        event.preventDefault();
        handlers.onPreset(presetByKey[event.key]);
        return;
      }

      switch (event.key) {
        case "Tab":
          event.preventDefault();
          handlers.onCycle(event.shiftKey ? -1 : 1);
          break;
        case "ArrowLeft":
          event.preventDefault();
          handlers.onNudge(-1);
          break;
        case "ArrowRight":
          event.preventDefault();
          handlers.onNudge(1);
          break;
        case "ArrowDown":
          event.preventDefault();
          handlers.onNudge(-10);
          break;
        case "ArrowUp":
          event.preventDefault();
          handlers.onNudge(10);
          break;
        case "r":
        case "R":
          handlers.onReset();
          break;
        case "s":
        case "S":
          handlers.onSpeech();
          break;
        case "b":
        case "B":
          handlers.onBlink();
          break;
      }
    };

    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [handlers]);
}

