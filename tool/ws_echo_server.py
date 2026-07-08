#!/usr/bin/env python3
# WebSocket echo server for the Net Test app (flash/app/tool/net_test.app.rb).
#
# Setup (Ubuntu):
#   sudo apt install python3-websockets
#   (or: python3 -m venv ~/.venv-ws && ~/.venv-ws/bin/pip install websockets)
# Run:
#   python3 tool/ws_echo_server.py [port]
# Then set WS_URL in net_test.app.rb to ws://<this PC's IP>:8765/

import asyncio
import sys

import websockets


# websockets v10 passes (websocket, path); v11+ passes (websocket)
async def echo(websocket, path=None):
    async for message in websocket:
        print(f"echo: {message!r}")
        await websocket.send(message)


async def main(port):
    async with websockets.serve(echo, "0.0.0.0", port):
        print(f"WebSocket echo server listening on 0.0.0.0:{port}")
        await asyncio.Future()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    asyncio.run(main(port))
