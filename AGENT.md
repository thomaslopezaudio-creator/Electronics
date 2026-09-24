# Electronics

Personal electronics projects and learning. Thomas is building these to learn,
not just to get a working result — explain the *why* alongside the *what*.

## How Thomas prefers to work and learn

Feedback he has given, kept current. If a new instruction would conflict with one
already here, ask him before editing this section.

- **One step at a time.** When there are multiple steps, give only the most
  immediate task or item. Do not list the whole sequence up front.
- **Read the logs before theorising about hardware.** A week was lost on
  `led-strip` guessing at power and logic levels while the boot log had been
  printing the actual cause the whole time.
- **Tooling:** VS Code + PlatformIO. Not Arduino IDE.
- **Bench webcam:** he watches it through OBS. Never open the webcam from a
  script (ffmpeg/avfoundation) — it disrupts his OBS feed. Use `tools/obs_snap.py`
  instead. The Claude app has no Camera or Screen Recording permission and he
  doesn't want to grant them.
- **Pin dependency versions exactly** (see `led-strip/NOTES.md` §2).

## Shared tools

- `tools/obs_snap.py` — grab a still from an OBS source over obs-websocket
  (127.0.0.1:4455, auth disabled). This is how Claude sees the bench.

  ```bash
  uv run --with websockets python tools/obs_snap.py Desktop out.png 1280
  ```

  Sources: `Desktop` = overhead bench cam, `Face Cam` = face cam, `--list` to
  enumerate. Requires OBS running with Tools → WebSocket Server Settings enabled.

## Projects

### `led-strip/` — done, parked

ESP32-S3-N16R8 driving 150 WS2812B LEDs around the desk and pegboard, with a
local web app (scenes + daily schedule) on the WiFi at **http://192.168.1.50**.
Built on proto board. Firmware updates over the air:

```bash
cd led-strip && pio run -e strip-ota -t upload
```

- `README.md` — wiring, scenes, schedule, HTTP API, troubleshooting table.
- `NOTES.md` — build log: brownouts, the FastLED version trap, 3.3 V vs 5 V
  logic levels, mDNS stalls. Read before starting another LED or ESP32 build.
- Outstanding: add a 74AHCT125 buffer on the data line so the supply can go back
  to 5.0 V (currently 4.6 V as a workaround).

### `bench-cam/` — superseded

Python CLI that captures webcam frames with ffmpeg and asks Claude about them.
Superseded by `tools/obs_snap.py`: ffmpeg grabs the camera device directly, which
fights OBS for it. Don't run it while OBS is open.

<!-- New projects: add a `### <folder>/` section here. -->
