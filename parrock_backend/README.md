# Parrock backend

FastAPI receives HTTP audio uploads and stores them on disk.
An MQTT client listens to `parrock/sensors` and appends every message to a JSONL file.

## What this includes

- FastAPI app with `/upload_audio`
- Static serving for `/recordings/latest.raw`
- MQTT subscriber running in the same process
- File-based storage:
  - `data/recordings/latest.raw`
  - `data/recordings/audio_*.raw`
  - `data/recordings/uploads.jsonl`
  - `data/mqtt/messages.jsonl`

## Install

```bash
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

## Run

Make sure an MQTT broker is running and reachable from your ESP32 and from this PC.

```bash
uvicorn app.main:app --host 0.0.0.0 --port 8080
```

## Test MQTT locally

In one terminal:

```bash
mosquitto_sub -h 127.0.0.1 -t parrock/sensors -v
```

In another terminal:

```bash
mosquitto_pub -h 127.0.0.1 -t parrock/sensors -m '{"event":"MOTION","battery":85}'
```

## ESP32 notes

Your ESP32 must use the PC's LAN IP, not `127.0.0.1`, because the board cannot reach localhost on the PC.

Example:

- `MQTT_BROKER_URI = mqtt://192.168.1.100`
- `HTTP_AUDIO_UPLOAD_URL = http://192.168.1.100:8080/upload_audio`
- `HTTP_AUDIO_PUBLIC_URL = http://192.168.1.100:8080/recordings/latest.raw`

## Folder layout

```
app/
  config.py
  main.py
  mqtt_service.py
  storage.py
data/
  mqtt/
  recordings/
```
