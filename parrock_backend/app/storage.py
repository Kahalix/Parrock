from __future__ import annotations

import json
import threading
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


_write_lock = threading.Lock()


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def append_jsonl(path: Path, payload: dict[str, Any]) -> None:
    ensure_dir(path.parent)
    line = json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
    with _write_lock:
        with path.open("a", encoding="utf-8") as f:
            f.write(line + "\n")


def write_bytes(path: Path, data: bytes) -> None:
    ensure_dir(path.parent)
    with _write_lock:
        path.write_bytes(data)


def make_timestamped_filename(prefix: str, suffix: str) -> str:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    return f"{prefix}_{stamp}{suffix}"


def safe_decode_bytes(data: bytes) -> str:
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return data.decode("utf-8", errors="replace")
