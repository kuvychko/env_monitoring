"""MQTT client for subscribing to IAQ telemetry."""

import json
import logging
import os
from datetime import datetime, timezone
from typing import Callable

import paho.mqtt.client as mqtt

logger = logging.getLogger(__name__)

# MQTT configuration from environment
MQTT_HOST = os.getenv("MQTT_HOST", "localhost")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_TOPIC = os.getenv("MQTT_TOPIC", "iaq/+/telemetry")

# Callback type for processing messages
MessageHandler = Callable[[dict], None]


def parse_payload(payload: bytes) -> dict | None:
    """
    Parse MQTT payload JSON into a database-ready dict.

    Returns None if parsing fails.
    """
    try:
        data = json.loads(payload.decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as e:
        logger.warning(f"Failed to parse payload: {e}")
        return None

    # Validate required fields
    if "device_id" not in data:
        logger.warning("Payload missing device_id")
        return None

    # Parse or generate timestamp
    if "ts" in data and data["ts"]:
        try:
            ts = datetime.fromisoformat(data["ts"].replace("Z", "+00:00"))
        except ValueError:
            logger.warning(f"Invalid timestamp format: {data['ts']}, using server time")
            ts = datetime.now(timezone.utc)
    else:
        ts = datetime.now(timezone.utc)

    # Map payload to database columns
    # Use None for missing/invalid values (will be NULL in DB)
    def get_float(key: str) -> float | None:
        val = data.get(key)
        if val is None or val == -1 or val == -1.0:
            return None
        try:
            return float(val)
        except (TypeError, ValueError):
            return None

    def get_int(key: str) -> int | None:
        val = data.get(key)
        if val is None or val == -1:
            return None
        try:
            return int(val)
        except (TypeError, ValueError):
            return None

    return {
        "time": ts,
        "device_id": data["device_id"],
        "co2_ppm": get_int("co2_ppm"),
        "temp_c": get_float("temp_c"),
        "rh_pct": get_float("rh_pct"),
        "pressure_hpa": get_float("pressure_hpa"),
        "pm1_0_ugm3": get_float("pm1_0_ugm3"),
        "pm2_5_ugm3": get_float("pm2_5_ugm3"),
        "pm4_0_ugm3": get_float("pm4_0_ugm3"),
        "pm10_ugm3": get_float("pm10_ugm3"),
        "nc0_5_pcm3": get_float("nc0_5_pcm3"),
        "nc1_0_pcm3": get_float("nc1_0_pcm3"),
        "nc2_5_pcm3": get_float("nc2_5_pcm3"),
        "nc4_0_pcm3": get_float("nc4_0_pcm3"),
        "nc10_pcm3": get_float("nc10_pcm3"),
        "typical_size_um": get_float("typical_size_um"),
        "rssi_dbm": get_int("rssi_dbm"),
        "uptime_s": get_int("uptime_s"),
        # CO2 sensor diagnostics
        "co2_age_s": get_int("co2_age_s"),
        "co2_resets": get_int("co2_resets"),
    }


def create_client(on_message: MessageHandler) -> mqtt.Client:
    """
    Create and configure an MQTT client.

    Args:
        on_message: Callback function to handle parsed messages
    """
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)

    def on_connect(client, userdata, flags, reason_code, properties):
        if reason_code == 0:
            logger.info(f"Connected to MQTT broker {MQTT_HOST}:{MQTT_PORT}")
            client.subscribe(MQTT_TOPIC, qos=1)
            logger.info(f"Subscribed to {MQTT_TOPIC}")
        else:
            logger.error(f"MQTT connection failed: {reason_code}")

    def on_disconnect(client, userdata, flags, reason_code, properties):
        logger.warning(f"Disconnected from MQTT broker: {reason_code}")

    def on_message_callback(client, userdata, msg):
        logger.debug(f"Received message on {msg.topic}")
        parsed = parse_payload(msg.payload)
        if parsed:
            on_message(parsed)

    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message_callback

    return client


def run_client(client: mqtt.Client) -> None:
    """Connect and run the MQTT client loop (blocking)."""
    logger.info(f"Connecting to MQTT broker {MQTT_HOST}:{MQTT_PORT}...")
    client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
    client.loop_forever()
