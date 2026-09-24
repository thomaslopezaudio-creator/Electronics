"""bench-cam command line: devices | snap | ask"""

from __future__ import annotations

import argparse
import sys
from datetime import datetime
from pathlib import Path

from . import ask as ask_mod
from . import camera


def add_capture_args(p: argparse.ArgumentParser) -> None:
    p.add_argument("-d", "--device", help=f"camera index or name substring (default: {camera.DEFAULT_DEVICE})")
    p.add_argument("-s", "--size", default=camera.DEFAULT_SIZE, help=f"capture WxH (default: {camera.DEFAULT_SIZE})")
    p.add_argument("-c", "--crop", metavar="X,Y,W,H", help="crop region in pixels of the captured frame")
    p.add_argument("--warmup", type=int, default=camera.WARMUP_FRAMES, help="frames to discard for auto-exposure")


def cmd_devices(_args: argparse.Namespace) -> int:
    for d in camera.list_devices():
        print(f"[{d.index}] {d.name}")
    return 0


def cmd_snap(args: argparse.Namespace) -> int:
    out = camera.capture(
        Path(args.output) if args.output else None,
        device=args.device, size=args.size, crop=args.crop, warmup=args.warmup,
    )
    print(out)
    return 0


def cmd_ask(args: argparse.Namespace) -> int:
    if args.image:
        image = Path(args.image)
        if not image.exists():
            print(f"bench-cam: image not found: {image}", file=sys.stderr)
            return 1
    else:
        image = camera.capture(device=args.device, size=args.size, crop=args.crop, warmup=args.warmup)
        print(f"[captured {image}]", file=sys.stderr)

    attachments = [Path(a) for a in args.attach]
    chunks: list[str] = []
    for text in ask_mod.ask(args.question, image, attachments, model=args.model, effort=args.effort):
        chunks.append(text)
        print(text, end="", flush=True)
    print()

    if not args.no_log:
        log = image.with_suffix(".md")
        with log.open("a") as f:
            f.write(f"## {datetime.now():%Y-%m-%d %H:%M:%S}\n\n")
            if attachments:
                f.write("Attachments: " + ", ".join(a.name for a in attachments) + "\n\n")
            f.write(f"**Q:** {args.question}\n\n{''.join(chunks).strip()}\n\n")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="bench-cam", description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("devices", help="list video devices").set_defaults(fn=cmd_devices)

    p = sub.add_parser("snap", help="capture one frame to captures/<timestamp>.jpg")
    add_capture_args(p)
    p.add_argument("-o", "--output", help="output path (default: captures/<timestamp>.jpg)")
    p.set_defaults(fn=cmd_snap)

    p = sub.add_parser("ask", help="capture a frame and ask Claude about it")
    add_capture_args(p)
    p.add_argument("question")
    p.add_argument("-i", "--image", help="use this image instead of capturing")
    p.add_argument("-a", "--attach", action="append", default=[], metavar="FILE",
                   help="attach a sketch, schematic, log, PDF or image (repeatable)")
    p.add_argument("--model", default=ask_mod.MODEL)
    p.add_argument("--effort", default="high", choices=["low", "medium", "high", "xhigh", "max"])
    p.add_argument("--no-log", action="store_true", help="don't append Q/A to captures/<stamp>.md")
    p.set_defaults(fn=cmd_ask)

    args = parser.parse_args(argv)
    try:
        return args.fn(args)
    except (camera.CameraError, ask_mod.AskError) as e:
        print(f"bench-cam: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
