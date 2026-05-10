import type { AvatarFeature, AvatarState, MouthFeature } from "./state";

const width = 320;
const height = 240;

export function drawAvatar(ctx: CanvasRenderingContext2D, state: AvatarState) {
  ctx.clearRect(0, 0, width, height);
  drawBackground(ctx);
  drawSpeechBubble(ctx, state);
  drawEye(ctx, 90, 104, state.leftEye, state.blinkClosed);
  drawEye(ctx, 230, 104, state.rightEye, state.blinkClosed);
  drawMouth(ctx, state.mouth);
  drawViewportFrame(ctx);
}

function drawBackground(ctx: CanvasRenderingContext2D) {
  ctx.fillStyle = "#000000";
  ctx.fillRect(0, 0, width, height);
}

function drawEye(
  ctx: CanvasRenderingContext2D,
  baseX: number,
  baseY: number,
  feature: AvatarFeature,
  blinkClosed: boolean,
) {
  const x = baseX + feature.x * 0.16;
  const y = baseY + feature.y * 0.16;
  const eyeSize = mapRange(feature.size, -100, 100, 8, 32);
  const visibleRatio = blinkClosed ? 0.12 : Math.max(0.08, Math.min(1, feature.weight / 100));
  const radius = eyeSize / 2;
  const eyelidOffsetY = -eyeSize * visibleRatio;

  ctx.save();
  ctx.translate(x, y);
  ctx.rotate((feature.rotation / 10) * (Math.PI / 180));

  ctx.fillStyle = "#ffffff";
  ctx.beginPath();
  ctx.arc(0, 0, radius, 0, Math.PI * 2);
  ctx.fill();

  ctx.fillStyle = "#000000";
  ctx.beginPath();
  ctx.arc(0, eyelidOffsetY, radius, 0, Math.PI * 2);
  ctx.fill();

  ctx.restore();
}

function drawMouth(ctx: CanvasRenderingContext2D, feature: MouthFeature) {
  const x = 160 + feature.x * 0.16;
  const y = 146 + feature.y * 0.16;
  const open = Math.max(0, feature.weight / 100);
  const mouthWidth = mapRange(feature.weight, 0, 100, 90, 60);
  const mouthHeight = mapRange(feature.weight, 0, 100, 6, 50);
  const mouthRadius = mapRange(feature.weight, 0, 100, 0, 16);

  ctx.save();
  ctx.translate(x, y);
  ctx.rotate((feature.rotation / 10) * (Math.PI / 180));
  ctx.fillStyle = "#ffffff";
  roundedRect(ctx, -mouthWidth / 2, -mouthHeight / 2, mouthWidth, mouthHeight, mouthRadius);
  ctx.fill();

  ctx.restore();
}

function drawSpeechBubble(ctx: CanvasRenderingContext2D, state: AvatarState) {
  if (!state.speechVisible) {
    return;
  }

  ctx.save();
  ctx.fillStyle = "#f5f1e8";
  ctx.strokeStyle = "rgba(0,0,0,0.2)";
  ctx.lineWidth = 1;
  roundedRect(ctx, 26, 14, 150, 38, 10);
  ctx.fill();
  ctx.stroke();

  ctx.beginPath();
  ctx.moveTo(88, 50);
  ctx.lineTo(104, 64);
  ctx.lineTo(112, 48);
  ctx.closePath();
  ctx.fill();
  ctx.stroke();

  ctx.fillStyle = "#11161c";
  ctx.font = "600 14px ui-monospace, SFMono-Regular, Menlo, monospace";
  ctx.fillText("Hello from web", 42, 38);
  ctx.restore();
}

function drawViewportFrame(ctx: CanvasRenderingContext2D) {
  ctx.save();
  ctx.strokeStyle = "#3b3f46";
  ctx.lineWidth = 1;
  ctx.strokeRect(0.5, 0.5, width - 1, height - 1);
  ctx.restore();
}

function roundedRect(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  w: number,
  h: number,
  r: number,
) {
  const radius = Math.min(r, w / 2, h / 2);
  ctx.beginPath();
  ctx.moveTo(x + radius, y);
  ctx.arcTo(x + w, y, x + w, y + h, radius);
  ctx.arcTo(x + w, y + h, x, y + h, radius);
  ctx.arcTo(x, y + h, x, y, radius);
  ctx.arcTo(x, y, x + w, y, radius);
  ctx.closePath();
}

function mapRange(value: number, inMin: number, inMax: number, outMin: number, outMax: number) {
  return outMin + ((value - inMin) * (outMax - outMin)) / (inMax - inMin);
}
