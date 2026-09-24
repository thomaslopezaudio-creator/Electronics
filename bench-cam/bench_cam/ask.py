"""Send a bench photo (plus optional files) to Claude and stream the answer."""

from __future__ import annotations

import base64
import mimetypes
import os
import sys
from pathlib import Path
from typing import Iterator

import anthropic

MODEL = os.environ.get("BENCH_CAM_MODEL", "claude-opus-5")
MAX_TOKENS = 16000
NO_CREDS = (
    "no valid Anthropic credentials. Export ANTHROPIC_API_KEY, or install the `ant` CLI "
    "and run `ant auth login`."
)

SYSTEM_PROMPT = """\
You are a bench assistant for someone learning and building electronics, mostly
ESP32 and Raspberry Pi projects. You are looking at a photo from a webcam
pointed at their workbench, possibly with attached schematics, code, datasheet
excerpts, or serial logs.

How to work:
- Describe what you can actually see before drawing conclusions. If wiring,
  pin labels, or component markings are unreadable at this resolution, say so
  and suggest a crop/closer shot rather than guessing.
- Check for the usual bench mistakes: VCC/GND swaps, missing common ground,
  3.3V vs 5V logic mismatches, missing pull-ups on I2C, wrong ESP32 pins
  (strapping pins 0/2/12/15, input-only GPIO34-39, flash pins 6-11), reversed
  polarity on electrolytics/LEDs, breadboard rails split in the middle.
- When identifying components, give the reading and how you got it (color
  bands, SMD code, package) so the user can verify.
- Be concrete and brief. Prefer a short checklist over prose. Call out anything
  that could damage hardware first.
"""

TEXT_EXTS = {
    ".txt", ".md", ".log", ".ino", ".c", ".h", ".cpp", ".hpp", ".py", ".yaml", ".yml",
    ".json", ".toml", ".ini", ".cfg", ".csv", ".sh", ".rs", ".js", ".ts", ".kicad_sch",
    ".net", ".cir", ".sp",
}
IMAGE_TYPES = {"image/jpeg", "image/png", "image/gif", "image/webp"}


class AskError(RuntimeError):
    pass


def _b64(path: Path) -> str:
    return base64.standard_b64encode(path.read_bytes()).decode("ascii")


def image_block(path: Path) -> dict:
    media_type = mimetypes.guess_type(path.name)[0]
    if media_type not in IMAGE_TYPES:
        raise AskError(f"{path}: unsupported image type {media_type}")
    return {"type": "image", "source": {"type": "base64", "media_type": media_type, "data": _b64(path)}}


def attachment_blocks(path: Path) -> list[dict]:
    """Turn an attached file into content blocks: image, PDF document, or inline text."""
    if not path.exists():
        raise AskError(f"attachment not found: {path}")
    media_type = mimetypes.guess_type(path.name)[0]
    if media_type in IMAGE_TYPES:
        return [{"type": "text", "text": f"Attached image: {path.name}"}, image_block(path)]
    if media_type == "application/pdf":
        return [
            {"type": "text", "text": f"Attached document: {path.name}"},
            {"type": "document", "source": {"type": "base64", "media_type": "application/pdf", "data": _b64(path)}},
        ]
    if path.suffix.lower() in TEXT_EXTS or media_type is None or media_type.startswith("text/"):
        try:
            text = path.read_text(errors="replace")
        except OSError as e:
            raise AskError(f"{path}: {e}") from None
        return [{"type": "text", "text": f'<file name="{path.name}">\n{text}\n</file>'}]
    raise AskError(f"{path}: don't know how to attach {media_type}")


def build_content(question: str, image: Path, attachments: list[Path]) -> list[dict]:
    content: list[dict] = [
        {"type": "text", "text": f"Bench camera frame ({image.name}):"},
        image_block(image),
    ]
    for a in attachments:
        content.extend(attachment_blocks(a))
    content.append({"type": "text", "text": question})
    return content


def ask(
    question: str,
    image: Path,
    attachments: list[Path] | None = None,
    *,
    model: str = MODEL,
    effort: str = "high",
) -> Iterator[str]:
    """Yield answer text as it streams. Raises AskError on API problems."""
    client = anthropic.Anthropic()
    if client.api_key is None and client.auth_token is None and getattr(client, "credentials", None) is None:
        raise AskError(NO_CREDS)
    content = build_content(question, image, attachments or [])

    try:
        with client.beta.messages.stream(
            model=model,
            max_tokens=MAX_TOKENS,
            system=SYSTEM_PROMPT,
            messages=[{"role": "user", "content": content}],
            thinking={"type": "adaptive"},
            output_config={"effort": effort},
            # Server-side fallback: if a safety classifier declines the request,
            # the API re-runs it on a substitute model inside the same call.
            betas=["server-side-fallback-2026-07-01"],
            fallbacks="default",
        ) as stream:
            for text in stream.text_stream:
                yield text
            final = stream.get_final_message()
    except anthropic.AuthenticationError:
        raise AskError(NO_CREDS) from None
    except anthropic.RateLimitError:
        raise AskError("rate limited by the API; wait a bit and retry") from None
    except anthropic.APIStatusError as e:
        raise AskError(f"API error {e.status_code}: {e.message}") from None
    except anthropic.APIConnectionError as e:
        raise AskError(f"could not reach the API: {e}") from None

    if final.stop_reason == "refusal":
        details = getattr(final, "stop_details", None)
        why = f" ({details.explanation})" if details and getattr(details, "explanation", None) else ""
        raise AskError(f"the model declined to answer{why}")
    if final.stop_reason == "max_tokens":
        print("\n[answer truncated at max_tokens]", file=sys.stderr)
