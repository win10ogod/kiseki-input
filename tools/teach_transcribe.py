#!/usr/bin/env python3
"""Transcribe a teaching audio file with faster-whisper.

The C++ CLI validates the audio/helper paths. This helper downloads the selected
faster-whisper model into the requested local directory when it is missing, then
uses that local model path for transcription.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", required=True)
    parser.add_argument("--model-id", default="Systran/faster-whisper-large-v3")
    parser.add_argument("--download-if-missing", action="store_true")
    parser.add_argument("--audio", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--language", default="")
    parser.add_argument("--device", default="auto")
    parser.add_argument("--compute-type", default="auto")
    return parser.parse_args()


def model_present(path: Path) -> bool:
    return path.is_dir() and any(path.iterdir())


def ensure_model(path: Path, model_id: str, download_if_missing: bool) -> None:
    if model_present(path):
        return
    if path.exists() and not path.is_dir():
        raise RuntimeError(f"model path exists but is not a directory: {path}")
    if not download_if_missing:
        raise RuntimeError(f"model directory does not exist or is empty: {path}")

    try:
        from huggingface_hub import snapshot_download
    except Exception as error:
        raise RuntimeError(
            f"failed to import huggingface_hub for model download: {error}; "
            "install with: pip install faster-whisper"
        ) from error

    path.parent.mkdir(parents=True, exist_ok=True)
    snapshot_download(repo_id=model_id, local_dir=str(path))


def main() -> int:
    args = parse_args()
    try:
        from faster_whisper import WhisperModel
    except Exception as error:  # pragma: no cover - exercised by CLI smoke use.
        print(f"failed to import faster_whisper: {error}", file=sys.stderr)
        print("install with: pip install faster-whisper", file=sys.stderr)
        return 2

    model_path = Path(args.model)
    try:
        ensure_model(model_path, args.model_id, args.download_if_missing)
    except Exception as error:
        print(f"failed to prepare faster-whisper model: {error}", file=sys.stderr)
        return 2

    model = WhisperModel(
        str(model_path),
        device=args.device,
        compute_type=args.compute_type,
    )
    segments, info = model.transcribe(
        args.audio,
        language=args.language or None,
        vad_filter=True,
    )

    segment_items = []
    text_parts = []
    for index, segment in enumerate(segments):
        text = segment.text.strip()
        if text:
            text_parts.append(text)
        segment_items.append(
            {
                "index": index,
                "start": segment.start,
                "end": segment.end,
                "text": text,
            }
        )

    output = {
        "schemaVersion": 1,
        "kind": "kiseki-teach-transcript",
        "sourceAudio": str(Path(args.audio)),
        "model": str(model_path),
        "modelId": args.model_id,
        "language": getattr(info, "language", None),
        "languageProbability": getattr(info, "language_probability", None),
        "duration": getattr(info, "duration", None),
        "text": "\n".join(text_parts),
        "segments": segment_items,
    }

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
