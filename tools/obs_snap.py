#!/usr/bin/env python3
"""Grab a still from an OBS source via obs-websocket v5 (no auth, localhost).

usage: obs_snap.py [SOURCE] [OUT.png] [WIDTH]
       obs_snap.py --list
"""
import base64, json, sys, uuid
from websockets.sync.client import connect

URL = "ws://127.0.0.1:4455"

def rpc(ws, kind, data=None):
    ws.send(json.dumps({"op": 6, "d": {"requestType": kind, "requestId": str(uuid.uuid4()), "requestData": data or {}}}))
    while True:
        msg = json.loads(ws.recv())
        if msg["op"] == 7:
            if not msg["d"]["requestStatus"]["result"]:
                sys.exit(f"OBS error: {msg['d']['requestStatus']}")
            return msg["d"].get("responseData", {})

def main():
    with connect(URL, max_size=64 * 1024 * 1024) as ws:
        hello = json.loads(ws.recv())
        if "authentication" in hello["d"]:
            sys.exit("OBS websocket has authentication enabled; turn it off in Tools > WebSocket Server Settings")
        ws.send(json.dumps({"op": 1, "d": {"rpcVersion": 1}}))
        json.loads(ws.recv())  # Identified
        if "--list" in sys.argv:
            for s in rpc(ws, "GetInputList")["inputs"]:
                print(s["inputName"], "—", s["inputKind"])
            return
        source = sys.argv[1] if len(sys.argv) > 1 else "Desktop"
        out = sys.argv[2] if len(sys.argv) > 2 else f"{source.lower().replace(' ', '_')}.png"
        width = int(sys.argv[3]) if len(sys.argv) > 3 else 1280
        r = rpc(ws, "GetSourceScreenshot", {"sourceName": source, "imageFormat": "png", "imageWidth": width})
        png = base64.b64decode(r["imageData"].split(",", 1)[1])
        open(out, "wb").write(png)
        print(out, len(png), "bytes")

main()
