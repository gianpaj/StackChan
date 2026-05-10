import { useEffect, useRef } from "react";
import { drawAvatar } from "./avatar/draw";
import type { AvatarState } from "./avatar/state";

type AvatarCanvasProps = {
  state: AvatarState;
};

export function AvatarCanvas({ state }: AvatarCanvasProps) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    const ctx = canvas?.getContext("2d");
    if (!canvas || !ctx) {
      return;
    }

    const scale = window.devicePixelRatio || 1;
    canvas.width = 320 * scale;
    canvas.height = 240 * scale;
    canvas.style.width = "320px";
    canvas.style.height = "240px";
    ctx.setTransform(scale, 0, 0, scale, 0, 0);
    drawAvatar(ctx, state);
  }, [state]);

  return (
    <div className="viewport-shell" aria-label="Avatar viewport">
      <canvas ref={canvasRef} className="avatar-canvas" width={320} height={240} />
    </div>
  );
}

