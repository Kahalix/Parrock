from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Settings:
    mqtt_host: str = os.getenv("MQTT_HOST", "127.0.0.1")
    mqtt_port: int = int(os.getenv("MQTT_PORT", "1883"))
    mqtt_topic: str = os.getenv("MQTT_TOPIC", "parrock/sensors")
    mqtt_client_id: str = os.getenv("MQTT_CLIENT_ID", "parrock-fastapi")

    storage_dir: Path = Path(os.getenv("STORAGE_DIR", "data"))
    recordings_subdir: str = "recordings"
    mqtt_subdir: str = "mqtt"

    api_host: str = os.getenv("API_HOST", "0.0.0.0")
    api_port: int = int(os.getenv("API_PORT", "8080"))

    @property
    def recordings_dir(self) -> Path:
        return self.storage_dir / self.recordings_subdir

    @property
    def mqtt_dir(self) -> Path:
        return self.storage_dir / self.mqtt_subdir

    @property
    def latest_audio_path(self) -> Path:
        return self.recordings_dir / "latest.raw"


settings = Settings()
