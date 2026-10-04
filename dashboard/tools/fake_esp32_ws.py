"""
Stand-in for the ESP32's WebSocket server, so the dashboard's WiFi path
can be tested without any hardware. Pushes a reading to every connected
client on a timer, the same way the real firmware should (the client
never sends anything, the server just keeps pushing).

Dev tool only. Needs:  pip install websockets

Usage:
    python tools/fake_esp32_ws.py                 # flat schema, port 8765
    python tools/fake_esp32_ws.py --schema nested # Jennifer's draft shape
    python tools/fake_esp32_ws.py --interval 0.2

Then in config.json:  "wifi_url": "ws://127.0.0.1:8765/ws"
and run:              python app.py --source wifi
"""

import argparse
import asyncio
import json
import math
import time

from websockets.asyncio.server import serve


def build_reading(t, schema):
    pitch = round(8 * math.exp(-0.05 * t) * math.sin(0.5 * t), 2)
    roll = round(6 * math.exp(-0.05 * t) * math.cos(0.4 * t), 2)
    power = round(1.2 + 0.1 * math.sin(0.1 * t), 2)

    if schema == "flat":
        return {"pitch": pitch, "roll": roll, "power": power}

    health = {"state": 3, "active_faults": 0, "latched_faults": 0,
              "sample_time_ms": int(t * 1000), "age_ms": 0, "sequence": int(t * 10),
              "error_count": 0, "has_sample": True}
    return {
        "schema_version": 0, "contract_status": "draft",
        "device_uptime_ms": int(t * 1000), "sequence": int(t * 5), "valid": True,
        "imu": {"valid": True, "pitch_deg": pitch, "roll_deg": roll, "health": health},
        "power": {"valid": True, "load_voltage_v": 12.48, "current_a": 0.73,
                  "power_w": power, "shunt_voltage_mv": 73.0, "health": health},
    }


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--interval", type=float, default=0.5)
    parser.add_argument("--schema", choices=["flat", "nested"], default="flat")
    args = parser.parse_args()

    async def handler(websocket):
        print("client connected")
        start = time.monotonic()
        try:
            while True:
                reading = build_reading(time.monotonic() - start, args.schema)
                await websocket.send(json.dumps(reading))
                await asyncio.sleep(args.interval)
        except Exception:
            print("client disconnected")

    async with serve(handler, "0.0.0.0", args.port):
        print(f"fake ESP32 listening on ws://0.0.0.0:{args.port}/ws "
              f"({args.schema} schema, every {args.interval}s)")
        await asyncio.Future()


if __name__ == "__main__":
    asyncio.run(main())
