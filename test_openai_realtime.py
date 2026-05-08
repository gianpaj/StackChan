#!/usr/bin/env python3
"""
test_openai_realtime.py — mirrors exactly what hal_openai_webrtc.cpp sends,
so you can iterate on session.update parameters without flashing.

Usage:
  CONFIG_OPENAI_API_KEY=sk-... python3 test_openai_realtime.py
"""

import asyncio
import json
import os
import sys

import websockets

API_KEY = os.environ.get("CONFIG_OPENAI_API_KEY", "")
if not API_KEY:
    sys.exit("ERROR: set CONFIG_OPENAI_API_KEY env var")

URL = "wss://api.openai.com/v1/realtime?model=gpt-realtime-2"

# ── mirrors _configure_session() in hal_openai_webrtc.cpp ────────────────────
SESSION_UPDATE = {
    "type": "session.update",
    "session": {
        "type": "realtime",
        "instructions": (
            "You are StackChan, a friendly robot companion. "
            "Keep responses short and conversational."
        ),
        "audio": {
            "input": {
                "turn_detection": {
                    "type": "server_vad",
                    "threshold": 0.8,
                    "silence_duration_ms": 600,
                    "interrupt_response": True,
                }
            }
        },
    },
}
# ─────────────────────────────────────────────────────────────────────────────


def _pp(label: str, obj: dict) -> None:
    tag = obj.get("type", "?")
    if "error" in obj:
        err = obj["error"]
        print(f"  ✗ ERROR [{err.get('code', '?')}] {err.get('message', '')}")
    else:
        print(f"  ✓ {tag}")
    if "--verbose" in sys.argv or "--v" in sys.argv:
        print(json.dumps(obj, indent=4))


async def main() -> None:
    headers = {"Authorization": f"Bearer {API_KEY}"}

    print(f"Connecting to {URL} …")
    async with websockets.connect(URL, additional_headers=headers) as ws:
        # ── 1. Receive session.created ────────────────────────────────────
        raw = await ws.recv()
        msg = json.loads(raw)
        _pp("session.created", msg)
        if msg.get("type") != "session.created":
            print("  Unexpected first message — aborting")
            return

        # ── 2. Send session.update (same as ESP32) ────────────────────────
        payload = json.dumps(SESSION_UPDATE)
        print("\nSending session.update …")
        if "--verbose" in sys.argv or "--v" in sys.argv:
            print(json.dumps(SESSION_UPDATE, indent=4))
        await ws.send(payload)

        # ── 3. Collect responses for a few seconds ────────────────────────
        print("\nWaiting for server response(s) …\n")
        deadline = asyncio.get_event_loop().time() + 5.0
        while asyncio.get_event_loop().time() < deadline:
            try:
                raw = await asyncio.wait_for(ws.recv(), timeout=5.0)
            except asyncio.TimeoutError:
                break
            msg = json.loads(raw)
            _pp(msg.get("type", "?"), msg)
            t = msg.get("type", "")
            if t == "session.updated":
                print("\n  session.update accepted — parameters are valid.")
                break
            if "error" in msg:
                print("\n  session.update REJECTED by server (see error above).")
                print("\n  Current session.update payload sent:")
                print(json.dumps(SESSION_UPDATE["session"], indent=4))
                break


asyncio.run(main())
