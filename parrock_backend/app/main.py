from __future__ import annotations

import os
import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles
from starlette.requests import ClientDisconnect

from .config import settings
from .mqtt_service import MQTTService
from .storage import (
    append_jsonl,
    ensure_dir,
    make_timestamped_filename,
    utc_now_iso,
    write_bytes,
)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s: %(message)s",
)
logger = logging.getLogger("parrock.api")


@asynccontextmanager
async def lifespan(app: FastAPI):
    ensure_dir(settings.storage_dir)
    ensure_dir(settings.recordings_dir)
    ensure_dir(settings.mqtt_dir)

    mqtt_log_path = settings.mqtt_dir / "messages.jsonl"
    app.state.mqtt = MQTTService(settings=settings, mqtt_log_path=mqtt_log_path)
    app.state.mqtt.start()

    logger.info("FastAPI startup complete")
    try:
        yield
    finally:
        logger.info("Shutting down MQTT client")
        app.state.mqtt.stop()


app = FastAPI(title="Parrock backend", lifespan=lifespan)

app.mount("/recordings", StaticFiles(directory=str(settings.recordings_dir)), name="recordings")


@app.get("/health")
async def health():
    mqtt_health = app.state.mqtt.health() if hasattr(app.state, "mqtt") else {"connected": False}
    return {
        "status": "ok",
        "time": utc_now_iso(),
        "mqtt": mqtt_health,
    }


@app.post("/upload_audio")
async def upload_audio(request: Request):
    ensure_dir(settings.recordings_dir)

    latest_path = settings.latest_audio_path
    timestamped_name = make_timestamped_filename("audio", ".raw")
    timestamped_path = settings.recordings_dir / timestamped_name

    total = 0
    try:
        # Process the incoming stream in chunks to avoid loading
        # the entire payload into the server's RAM at once (OOM protection).
        with latest_path.open("wb") as latest_f, timestamped_path.open("wb") as ts_f:
            async for chunk in request.stream():
                if not chunk:
                    continue
                latest_f.write(chunk)
                ts_f.write(chunk)
                total += len(chunk)
    except ClientDisconnect:
        logger.warning("Client disconnected during upload after %d bytes", total)

    if total == 0:
        logger.warning("Upload failed: no data received. Cleaning up...")
        if timestamped_path.exists():
            os.remove(timestamped_path)
        if latest_path.exists() and latest_path.stat().st_size == 0:
            os.remove(latest_path)

        raise HTTPException(status_code=400, detail="Empty body or connection dropped")

    meta = {
        "ts": utc_now_iso(),
        "bytes": total,
        "latest": latest_path.name,
        "file": timestamped_name,
        "url": f"/recordings/{timestamped_name}",
    }
    append_jsonl(settings.recordings_dir / "uploads.jsonl", meta)

    logger.info("Audio received: %s bytes -> %s", total, timestamped_name)
    return JSONResponse(
        {
            "status": "ok",
            "bytes": total,
            "file": timestamped_name,
            "latest_url": "/recordings/latest.raw",
            "file_url": f"/recordings/{timestamped_name}",
        }
    )

@app.get("/mqtt/latest")
async def mqtt_latest():
    mqtt_file = settings.mqtt_dir / "messages.jsonl"
    if not mqtt_file.exists():
        return {"items": []}

    lines = mqtt_file.read_text(encoding="utf-8").splitlines()
    tail = lines[-20:]
    return {"items": [line for line in tail if line.strip()]}


@app.get("/")
async def root():
    return {
        "service": "parrock-backend",
        "docs": "/docs",
        "health": "/health",
        "upload_audio": "/upload_audio",
        "latest_audio": "/recordings/latest.raw",
    }
