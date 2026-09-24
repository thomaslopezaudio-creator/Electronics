# bench-cam

Point a webcam at your electronics bench and ask Claude about what it sees.

```bash
uv run bench-cam devices                      # list cameras
uv run bench-cam snap                         # captures/<timestamp>.jpg
uv run bench-cam snap -s 3840x2160 -c 1200,800,1400,1000   # 4K + crop for pin-level detail
uv run bench-cam ask "Is this ESP32 wired correctly for I2C?" -a sketch.ino -a wiring.png
uv run bench-cam ask -i captures/20260916-2230.jpg "What value is the resistor near the LED?"
```

Q/A is appended to `captures/<timestamp>.md` next to the frame, so the captures
folder doubles as a build log.

## Setup

- `ffmpeg` on PATH (Homebrew's is fine).
- Credentials: `export ANTHROPIC_API_KEY=...` or `ant auth login`.
- macOS Camera permission for whatever runs the command (Terminal, Claude, ...):
  System Settings > Privacy & Security > Camera. Without it ffmpeg blocks
  silently; `bench-cam` times out after 20s with a hint.

## Env overrides

| var | default | meaning |
|---|---|---|
| `BENCH_CAM_DEVICE` | `Depstech` | camera index or name substring |
| `BENCH_CAM_SIZE` | `1920x1080` | capture resolution |
| `BENCH_CAM_WARMUP` | `15` | frames discarded for auto-exposure |
| `BENCH_CAM_MODEL` | `claude-opus-5` | model for `ask` |
