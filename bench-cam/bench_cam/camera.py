"""Frame capture via ffmpeg's avfoundation input (macOS)."""

from __future__ import annotations

import os
import re
import subprocess
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

FFMPEG = os.environ.get("BENCH_CAM_FFMPEG", "ffmpeg")
DEFAULT_DEVICE = os.environ.get("BENCH_CAM_DEVICE", "Depstech")
DEFAULT_SIZE = os.environ.get("BENCH_CAM_SIZE", "1920x1080")
# UVC webcams auto-expose over the first several frames; the first one is
# usually dark or green. Grab a handful and keep the last.
WARMUP_FRAMES = int(os.environ.get("BENCH_CAM_WARMUP", "15"))
CAPTURE_TIMEOUT = 20  # seconds; avfoundation blocks forever without camera permission


class CameraError(RuntimeError):
    pass


@dataclass
class Device:
    index: int
    name: str


def list_devices() -> list[Device]:
    """Parse `ffmpeg -f avfoundation -list_devices true` into video devices."""
    proc = subprocess.run(
        [FFMPEG, "-hide_banner", "-f", "avfoundation", "-list_devices", "true", "-i", ""],
        capture_output=True,
        text=True,
    )
    devices: list[Device] = []
    in_video = False
    for line in proc.stderr.splitlines():
        if "AVFoundation video devices" in line:
            in_video = True
            continue
        if "AVFoundation audio devices" in line:
            break
        if in_video:
            m = re.search(r"\[(\d+)\] (.+)$", line)
            if m:
                devices.append(Device(int(m.group(1)), m.group(2).strip()))
    if not devices:
        raise CameraError("ffmpeg found no avfoundation video devices")
    return devices


def resolve_device(spec: str | None) -> Device:
    """Accept an index ("0") or a case-insensitive name substring ("depstech").

    Names are preferred over indices because avfoundation renumbers devices
    whenever an iPhone or another camera comes and goes.
    """
    spec = spec or DEFAULT_DEVICE
    devices = list_devices()
    if spec.isdigit():
        for d in devices:
            if d.index == int(spec):
                return d
        raise CameraError(f"no video device with index {spec}")
    matches = [d for d in devices if spec.lower() in d.name.lower()]
    if not matches:
        names = ", ".join(f"[{d.index}] {d.name}" for d in devices)
        raise CameraError(f"no video device matching {spec!r}; available: {names}")
    return matches[0]


def parse_crop(spec: str) -> tuple[int, int, int, int]:
    """'x,y,w,h' -> tuple. Pixel coordinates in the captured frame."""
    try:
        x, y, w, h = (int(v) for v in spec.split(","))
    except ValueError:
        raise CameraError(f"crop must be x,y,w,h in pixels, got {spec!r}") from None
    return x, y, w, h


def capture(
    out: Path | None = None,
    *,
    device: str | None = None,
    size: str = DEFAULT_SIZE,
    crop: str | None = None,
    warmup: int = WARMUP_FRAMES,
) -> Path:
    """Grab one JPEG frame and return its path."""
    dev = resolve_device(device)
    if out is None:
        out = Path("captures") / f"{datetime.now():%Y%m%d-%H%M%S}.jpg"
    out.parent.mkdir(parents=True, exist_ok=True)

    cmd = [
        FFMPEG, "-hide_banner", "-loglevel", "error", "-y",
        "-f", "avfoundation",
        "-framerate", "30",
        "-video_size", size,
        "-i", str(dev.index),
        "-frames:v", str(max(1, warmup)),
        "-update", "1",  # overwrite the same file so the last (warmed-up) frame wins
        "-q:v", "2",
    ]
    if crop:
        x, y, w, h = parse_crop(crop)
        cmd += ["-vf", f"crop={w}:{h}:{x}:{y}"]
    cmd.append(str(out))

    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=CAPTURE_TIMEOUT)
    except subprocess.TimeoutExpired:
        raise CameraError(
            f"timed out reading from [{dev.index}] {dev.name}. On macOS this almost always "
            "means the app running this command has no Camera permission: "
            "System Settings > Privacy & Security > Camera, then enable your terminal/app."
        ) from None
    if proc.returncode != 0 or not out.exists():
        raise CameraError(f"ffmpeg failed for [{dev.index}] {dev.name}:\n{proc.stderr.strip()}")
    return out
