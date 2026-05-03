from __future__ import annotations

import json
import logging
import threading
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import paho.mqtt.client as mqtt

from .config import Settings
from .storage import append_jsonl, safe_decode_bytes, utc_now_iso

logger = logging.getLogger("parrock.mqtt")


@dataclass
class MqttState:
    connected: bool = False
    last_error: str | None = None
    last_message_at: str | None = None


class MQTTService:
    def __init__(self, settings: Settings, mqtt_log_path: Path) -> None:
        self.settings = settings
        self.mqtt_log_path = mqtt_log_path
        self.state = MqttState()
        self._lock = threading.Lock()
        self.client = mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id=settings.mqtt_client_id,
            protocol=mqtt.MQTTv311,
            reconnect_on_failure=True,
        )
        self.client.reconnect_delay_set(min_delay=1, max_delay=30)
        self.client.on_connect = self._on_connect
        self.client.on_disconnect = self._on_disconnect
        self.client.on_message = self._on_message
        self.client.on_connect_fail = self._on_connect_fail

    def start(self) -> None:
        """
        Non-blocking connect + background network loop.
        The client will keep retrying if the broker is temporarily unavailable.
        """
        logger.info("Starting MQTT client for %s:%s", self.settings.mqtt_host, self.settings.mqtt_port)
        self.client.connect_async(self.settings.mqtt_host, self.settings.mqtt_port, keepalive=60)
        self.client.loop_start()

    def stop(self) -> None:
        try:
            self.client.loop_stop()
        finally:
            try:
                self.client.disconnect()
            except Exception:
                pass

    def publish_json(self, topic: str, payload: dict[str, Any], qos: int = 1, retain: bool = False) -> None:
        body = json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
        self.client.publish(topic, body, qos=qos, retain=retain)

    def _on_connect(self, client, userdata, flags, reason_code, properties=None) -> None:
        with self._lock:
            self.state.connected = True
            self.state.last_error = None

        logger.info("MQTT connected: reason_code=%s", reason_code)
        client.subscribe(self.settings.mqtt_topic, qos=1)

    def _on_disconnect(self, client, userdata, disconnect_flags, reason_code, properties=None) -> None:
        with self._lock:
            self.state.connected = False
        logger.warning("MQTT disconnected: reason_code=%s", reason_code)

    def _on_connect_fail(self, client, userdata) -> None:
        with self._lock:
            self.state.connected = False
            self.state.last_error = "connect failed"
        logger.warning("MQTT connection attempt failed")

    def _on_message(self, client, userdata, msg) -> None:
        raw = msg.payload or b""
        text = safe_decode_bytes(raw)

        parsed: Any = text
        try:
            parsed = json.loads(text)
        except json.JSONDecodeError:
            pass

        entry = {
            "ts": utc_now_iso(),
            "topic": msg.topic,
            "qos": msg.qos,
            "retain": bool(msg.retain),
            "payload_text": text,
            "payload": parsed,
        }
        append_jsonl(self.mqtt_log_path, entry)

        with self._lock:
            self.state.last_message_at = entry["ts"]

        logger.info("MQTT %s -> saved", msg.topic)

    def health(self) -> dict[str, Any]:
        with self._lock:
            return {
                "connected": self.state.connected,
                "last_error": self.state.last_error,
                "last_message_at": self.state.last_message_at,
            }
